from textwrap import dedent

import sys
import subprocess
from functools import wraps
import tempfile
import os
import shutil
from os.path import join as pjoin
from inspect import isgeneratorfunction
from pprint import pprint

from nose.tools import eq_, ok_

def strip_prefix(pre, s):
    if s.startswith(pre):
        return s[len(pre):]
    else:
        return None

def execute_link(cmd, path_entry=None):
    env = dict(os.environ)
    env['HDIST_LAUNCHER_DEBUG'] = '1'
    if path_entry is not None:
        env['PATH'] = os.pathsep.join([path_entry, env['PATH']])
    p = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, env=env)
    stdout, stderr = p.communicate()
    #print stderr
    result = {}
    for line in stderr.splitlines():
        line = strip_prefix("launcher:DEBUG:", line)
        if line is None:
            continue
        try:
            key, value = line.split('=')
        except ValueError:
            pass
        else:
            result.setdefault(key, []).append(value)
    return result, p.wait(), stdout.splitlines(), stderr.splitlines(), 

def fixture(func):
    if isgeneratorfunction(func):
        @wraps(func)
        def replacement():
            oldcwd = os.getcwd()
            d = os.path.realpath(tempfile.mkdtemp())
            try:
                os.chdir(d)
                for x in func(d):
                    yield x
            finally:
                shutil.rmtree(d)
                os.chdir(oldcwd)
    else:
        @wraps(func)
        def replacement():
            oldcwd = os.getcwd()
            d = os.path.realpath(tempfile.mkdtemp())
            try:
                os.chdir(d)
                func(d)
            finally:
                shutil.rmtree(d)
                os.chdir(oldcwd)
        
    return replacement

def setup():
    global _launcher
    subprocess.check_call(['make'])
    _launcher = os.path.realpath('launcher')

def formatlist(lst, **vars):
    return [item.format(**vars) for item in lst]

def touch(filename):
    with open(filename, 'w') as f:
        pass

@fixture
def test_link_resolution(d):
    os.symlink(_launcher, 'foo0')
    os.symlink('foo0', 'foo1')
    os.symlink(pjoin(d, 'foo1'), 'foo2')
    os.symlink('./foo2', 'foo3')

    def doit(cmd, path_entry, prefix):
        log, ret, _, lines = execute_link(cmd, path_entry)
        eq_(127, ret)
        eq_(formatlist(['{prefix}/foo3 -> ./foo2',
                        '{prefix}/./foo2 -> {d}/foo1',
                        '{d}/foo1 -> foo0',
                        '{d}/foo0 -> {_launcher}'], prefix=prefix, d=d, _launcher=_launcher),
            log['readlink'])
        ok_(lines[-1].startswith("launcher:Unable to launch '%s/foo0.real'" % d),
            lines[-1])

    yield doit, ['./foo3'], None, '.'
    yield doit, ['foo3'], '.', '.'
    yield doit, ['foo3'], d, d
    yield doit, [pjoin(d, 'foo3')], None, d

@fixture
def test_symlink_in_path(d):
    # opt/profile/bin/python -> launcher
    # opt/profile/bin/python.link -> ../../python/bin/python
    # foo/local -> ../opt/profile
    # execute foo/local/

    os.makedirs('opt/profile/bin')
    os.makedirs('opt/python/bin')
    os.makedirs('foo')
    os.symlink('../opt/profile', 'foo/local')
    os.symlink(_launcher, 'opt/profile/bin/python')
    with open('opt/profile/bin/python.link', 'w') as f:
        f.write('../../python/bin/python')
    log, ret, _, lines = execute_link(['python'], pjoin(d, 'foo/local/bin'))
    ok_(lines[-1].startswith("launcher:Unable to launch '%s/opt/profile/bin/../../python/bin/python'" % d))

@fixture
def test_profile_bin_dir(d):
    os.mkdir(pjoin(d, '1'))
    os.mkdir(pjoin(d, '2'))
    os.mkdir(pjoin(d, '3'))

    os.symlink(_launcher, pjoin(d, '1', 'foo'))
    os.symlink(pjoin(d, '1', 'foo'), pjoin(d, '2', 'foo'))
    os.symlink(pjoin(d, '2', 'foo'), pjoin(d, '3', 'foo'))

    log, ret, out, err = execute_link([pjoin(d, '3', 'foo')])
    assert log['PROFILE_BIN_DIR'] == ['']

    for p in ['1', '2', '3']:
        touch(pjoin(d, p, 'is-profile-bin'))
        log, ret, out, err = execute_link([pjoin(d, '3', 'foo')])
        assert log['PROFILE_BIN_DIR'] == [pjoin(d, p)]


@fixture
def test_shebang_parsing(d):
    # put script.real in a subdir, to make sure ORIGIN translates
    # to file location
    os.mkdir('realdir')
    os.mkdir('linkdir')
    def put_script(shebang):
        with open('realdir/script', 'w') as f:
            f.write(shebang)
            f.write('\npayload\n')
    os.symlink('../realdir/script', 'linkdir/script')

    with open('script.link', 'w') as f:
        f.write('linkdir/script')
    os.symlink(_launcher, 'script')

    put_script('#!${ORIGIN}/../foo a-${ORIGIN}${ORIGIN}-${ORIGIN}a \t\t  \t')

    log, ret, _, lines = execute_link('./script')
    eq_('{d}/realdir/../foo'.format(d=d), log['shebang_cmd'][0])
    eq_('a-{d}/realdir{d}/realdir-{d}/realdira'.format(d=d), log['shebang_arg'][0])

    put_script('#!${ORIGIN}/../foo  \t\t  \t')
    log, ret, _, lines = execute_link('./script')
    eq_('{d}/realdir/../foo'.format(d=d), log['shebang_cmd'][0])
    eq_('', log['shebang_arg'][0])

    put_script('#!${ORIGIN}/../foo')
    log, ret, _, lines = execute_link('./script')
    eq_('{d}/realdir/../foo'.format(d=d), log['shebang_cmd'][0])

    put_script('#!${PROFILE_BIN_DIR}/../foo')
    log, ret, _, lines = execute_link('./script')
    eq_('__NA__/../foo', log['shebang_cmd'][0])

    touch('is-profile-bin')
    put_script('#!${PROFILE_BIN_DIR}/../foo')
    log, ret, _, lines = execute_link('./script')
    eq_('./../foo', log['shebang_cmd'][0])


@fixture
def test_shebang_running(d):
    with open('script.real', 'w') as f:
        f.write(dedent('''\
        #!${ORIGIN}/link-to-python
        import sys
        print("Hello world")
        print(":".join(sys.argv))
        sys.exit(3)
        '''))

    os.symlink(sys.executable, 'link-to-python')
    os.symlink(_launcher, 'script')

    log, ret, outlines, _ = execute_link(['./script', 'bar', 'foo'])
    eq_(3, ret)
    eq_(['Hello world', './script.real:bar:foo'], outlines)


@fixture
def test_shebang_multi(d):
    with open('script.real', 'w') as f:
        f.write(dedent('''\
        #!${ORIGIN}/link1:${ORIGIN}/link2
        import sys; sys.exit(3)
        '''))

    os.symlink(sys.executable, 'link1')
    os.symlink(_launcher, 'script')
    _, ret, out, err = execute_link(['./script'])
    eq_(3, ret)
    os.unlink('link1')
    os.symlink(sys.executable, 'link2')
    log, ret, _, lines = execute_link(['./script'])
    eq_(3, ret)
    

@fixture
def test_program_launching(d):
    with open('program.link', 'w') as f:
        f.write("/bin/echo\n")
    os.symlink(_launcher, 'program')
    log, ret, outlines, _ = execute_link(['./program', 'hello'])
    eq_(['hello'], outlines)

@fixture
def test_direct_execute(d):
    log, ret, outlines, errlines = execute_link([_launcher])
    eq_(0, ret)
    eq_([], outlines)
    ok_(any('Usage' in x for x in errlines))
