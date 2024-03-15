The text editor (the files file.c and file.h) are adapted from Krzysztof Czaja's
Cyclone package.  To avoid name clashes the prefix "hammer" was aliased to
"krzysz" (can't think of anything better that has 6 letters).

The script is
sed '/hammer/s/hammer/krzysz/g' \
    ~/work/czaja/cyclone/shared/hammer/file.c > file.c

