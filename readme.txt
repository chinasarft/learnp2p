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

cmake使用pkg-config查找库的，mac上openssl的pc文件都不在标准目录，所以需要设置
export PKG_CONFIG_PATH=/Users/liuye/Downloads/pjproject-2.7.2/mac/lib/pkgconfig:/usr/local/Cellar/openssl/1.0.2n/lib/pkgconfig
