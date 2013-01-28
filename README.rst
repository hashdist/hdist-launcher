hdist-launcher
==============

This is a very small program that facilitates launching applications
from relocateable ``bin`` directories, typically as the result of creating
Hashdist profiles. It has the following usecases:

**Process redirection (useful for relative sys.path for Python)**:
   Since the program is an executable and not a script, if ``hdist-launcher``
   is used to execute Python then it will first look in a location relative
   to the launcher executable for its libraries (``../lib/``), and if found
   use that as its library path ("the virtualenv technique")

**Relative shebangs**:
   If a script is launched, then the ``hdist-launcher`` will act
   directly as the process launcher and inspect the shebang. Shebangs
   are interpreted as by the OS, except if it has the following form::

       #!hdist-launcher $HDIST_LAUNCH_DIR/bin/python:../../a5df/python/bin/python

   That is, when the shebang starts with ``hdist-launcher``, then a)
   the interpreter locations are tried in turn, the next one is tried
   if the previous did not exist; b) ``$HDIST_LAUNCH_DIR`` is expanded
   to the directory containing ``hdist-launcher``, c) relative paths
   are interpreted relative to the script.

**Environment control**:
   For now, the ``HDIST_LAUNCH_DIR`` environment variable is set to the
   directory containing ``hdist-launcher`` (after resolving symlinks).
   More environment variable control may be added later if needed.

Usage
-----

In the same directory as ``hdist-launcher``, one must place a file
``hdist-launcher.cfg`` that looks like the following::

    # lines starting with # are comments
    # first line is name of link to hdist-launcher, second is what to
    # launch relative to the location of hdist-launcher
    python ../../python/a5df/bin/python
    cython cython.script

In this case, the ``bin`` directory could look like this::

    python -> hdist-launcher
    cython -> hdist-launcher
    hdist-launcher.cfg
    hdist-launcher

