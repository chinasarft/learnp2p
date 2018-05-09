cmake pkg-config 会设置一下变量
<XPREFIX>_FOUND          ... set to 1 if module(s) exist
<XPREFIX>_LIBRARIES      ... only the libraries (w/o the '-l')
<XPREFIX>_LIBRARY_DIRS   ... the paths of the libraries (w/o the '-L')
<XPREFIX>_LDFLAGS        ... all required linker flags
<XPREFIX>_LDFLAGS_OTHER  ... all other linker flags
<XPREFIX>_INCLUDE_DIRS   ... the '-I' preprocessor flags (w/o the '-I')
<XPREFIX>_CFLAGS         ... all required cflags
<XPREFIX>_CFLAGS_OTHER   ... the other compiler flags
<XPREFIX> = <PREFIX>        for common case
<XPREFIX> = <PREFIX>_STATIC for static linking

include_directoies不能写在add_executable的后面??
