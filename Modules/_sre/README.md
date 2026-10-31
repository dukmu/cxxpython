# SRE Adaptation

The reference is CPython 3.12.14 `Modules/_sre/`. `sre_constants.h` is unchanged;
`sre_lib.h` retains the matching algorithms with bounded native safepoint polls.
`engine.c` adapts the C state, allocation helpers and code validator without
Python object APIs. The public bridge is `engine.h`; object ownership and Python
protocols use this runtime's precise roots and tracing GC.

The original matching engine notice is retained here as well as in its headers:

Copyright (c) 1997-2001 by Secret Labs AB. All rights reserved.

This version of the SRE library can be redistributed under CNRI's
Python 1.6 license. For any other use, please contact Secret Labs
AB (info@pythonware.com).

Portions of this engine have been developed in cooperation with
CNRI. Hewlett-Packard provided funding for 1.6 integration and
other compatibility work.

The complete Python/PSF/CNRI license is retained in
[`doc/licenses/python-3.12.14.txt`](../../doc/licenses/python-3.12.14.txt).

Each match has private native state. Pattern instructions are immutable and
validated before publication. Native scratch has no managed references; the
caller roots the pattern and subject throughout execution. Poll callbacks catch
C++ exceptions before returning to C, and the C wrapper frees its stack/repeat
pools before the caller propagates an interruption.
