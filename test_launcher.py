import sys
import subprocess
from functools import wraps
import tempfile
import os
import shutil
from os.path import join as pjoin
from inspect import isgenerator

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
    @wraps(func)
    def replacement():
        oldcwd = os.getcwd()
        d = tempfile.mkdtemp()
        try:
            os.chdir(d)
            x = func(d)
            if isgenerator(x):
                for y in x:
                    yield y
        finally:
            shutil.rmtree(d)
            os.chdir(oldcwd)
    return replacement

def setup():
    global _launcher
    os.system('make')
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
        eq_(lines[-1], "hdist-launcher:Cannot open '%s/foo0.link'" % d)

    yield doit, ['./foo3'], None, '.'
    yield doit, ['foo3'], '.', '.'
    yield doit, ['foo3'], d, d
    yield doit, [pjoin(d, 'foo3')], None, d
