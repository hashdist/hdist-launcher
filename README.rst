Then Hashdist launcher
======================

This is a very small program that facilitates launching applications
from relocateable ``bin`` directories, typically as the result of creating
Hashdist profiles. It has the following usecases:


**Relative shebangs**:
   While Unix allows ``$!some/relative/path``, it
   is completely useless, as it uses the CWD(!). Using
   `launcher` allows for *proper* relative shebangs.  If a
   script is launched, then the ``launcher`` will act directly
   as the process launcher and inspect the shebang. Shebangs are
   interpreted as by the OS, except that the variable ``$PROFILE_BIN_DIR``
   and ``$ORIGIN`` is expanded, and one can try multiple interpreters,
   so that one can do::

    #!${PROFILE_BIN_DIR}/python:${ORIGIN}/../../../python/xkl4/bin/python

**Process redirection (useful for relocateable sys.prefix for Python)**:
   Since the program is an executable and not a script, if ``launcher``
   is used to execute Python then it will first look in a location relative
   to the launcher executable for its libraries (``../lib/``), and if found
   use that as its library path ("the virtualenv technique")

Usage
-----

When ``launcher`` is run we go through the following steps:

**1)**: ``argv[0]`` is used to find the last symlink pointing to
``launcher``; e.g., for::

    foo -> launcher
    bar -> foo

then the last symlink will be ``foo`` if either ``foo`` or ``bar``
is executed. It will be referred to as ``$launchlink`` below.

**2)**: The launcher looks for the file ``$launchlink.link``.
That file is read and the contents followed as if it was a symlink,
i.e. relative paths are relative to the location of the link file
(the reason for not using a symlink is to avoid cluttering up
PATH-based auto-completion).

If such a file is not found, it is assumed that the program/script to
execute is named ``$launchlink.real``.

**3)**: Once the program/script to be launched is decided on, we look
for the shebang ``#!``. If found, the launcher does the shebang launching
like the OS but with some extra features:

**Variable expansion**: ``${ORIGIN}`` is expanded to the real path
(using ``realpath()``) of the directory containing the script.
``${PROFILE_BIN_DIR}`` is expanded to the *first* directory in
the chain of links to contain a file named ``is-profile-bin``.

Secondly, it is allowed to have multiple interpreters that will be
tried in turn, separated by ``:``.

Example::

    #!${PROFILE_BIN_DIR}/python:${ORIGIN}/../../../python/xkl4/bin/python
    import sys
    ...

If a shebang is not found, a simple ``execv`` is done without changing
``argv``. The consequence of that is that if the program is Python,
it will think its binary is ``launcher``, and search for
``../lib/pythonX.Y`` relative to the ``launcher`` used.

Other
-----

Errors in the launcher causes return with code 127.


Testing
-------
Set ``HDIST_LAUNCHER_DEBUG=1`` for debug printing. Unit tests in
``nosetests test_launcher.py``

