from textwrap import dedent

import sys
import subprocess
from functools import wraps
import tempfile
import os
import shutil
from os.path import join as pjoin
from inspect import isgeneratorfunction

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
    p = subprocess.Popen(cmd, stderr=subprocess.PIPE, env=env)
    stdout, stderr = p.communicate()
    #print stderr
    result = {}
    for line in stderr.splitlines():
        line = strip_prefix("hdist-launcher:DEBUG:", line)
        if line is None:
            continue
        try:
            key, value = line.split('=')
        except ValueError:
            pass
        else:
            result.setdefault(key, []).append(value)
    return result, p.wait(), stderr.splitlines()

def fixture(func):
    if isgeneratorfunction(func):
        @wraps(func)
        def replacement():
            oldcwd = os.getcwd()
            d = tempfile.mkdtemp()
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
            d = tempfile.mkdtemp()
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
    _launcher = os.path.realpath('hdist-launcher')

def formatlist(lst, **vars):
    return [item.format(**vars) for item in lst]

@fixture
def test_link_resolution(d):
    os.symlink(_launcher, 'foo0')
    os.symlink('foo0', 'foo1')
    os.symlink(pjoin(d, 'foo1'), 'foo2')
    os.symlink('./foo2', 'foo3')

    def doit(cmd, path_entry, prefix):
        log, ret, lines = execute_link(cmd, path_entry)
        eq_(2, ret)
        eq_(formatlist(['{prefix}/foo3 -> ./foo2',
                        '{prefix}/./foo2 -> {d}/foo1',
                        '{d}/foo1 -> foo0',
                        '{d}/foo0 -> {_launcher}'], prefix=prefix, d=d, _launcher=_launcher),
            log['readlink'])
        ok_(lines[-1].startswith("hdist-launcher:Unable to launch '%s/foo0.real'" % d),
            lines[-1])

    yield doit, ['./foo3'], None, '.'
    yield doit, ['foo3'], '.', '.'
    yield doit, ['foo3'], d, d
    yield doit, [pjoin(d, 'foo3')], None, d

@fixture
def test_shebang_parsing(d):
    def put_script(shebang):
        with open('script.real', 'w') as f:
            f.write(shebang)
            f.write('\npayload\n')

    os.symlink(_launcher, 'script')

    put_script('#!${ORIGIN}/../foo a-${ORIGIN}${ORIGIN}-${ORIGIN}a \t\t  \t')
    log, ret, lines = execute_link('./script')
    eq_('./../foo', log['shebang_cmd'][0])
    eq_('a-..-.a', log['shebang_arg'][0])

    put_script('#!${ORIGIN}/../foo  \t\t  \t')
    log, ret, lines = execute_link('./script')
    eq_('', log['shebang_arg'][0])

    put_script('#!${ORIGIN}/../foo')
    log, ret, lines = execute_link('./script')

@fixture
def test_shebang_running(d):
    with open('script.real', 'w') as f:
        f.write(dedent('''\
        #!${ORIGIN}/link-to-python
        import sys
        sys.stderr.write("Hello world\\n")
        sys.stderr.write(":".join(sys.argv))
        sys.exit(3)
        '''))

    os.symlink(sys.executable, 'link-to-python')
    os.symlink(_launcher, 'script')

    log, ret, lines = execute_link(['./script', 'bar', 'foo'])
    eq_(3, ret)
    eq_('Hello world', lines[-2])
    eq_('./script.real:bar:foo', lines[-1])
