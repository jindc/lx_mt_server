gcc -W -Werror -g -I.  -I../lxlib -I../lx_http -I../lxlog   -l pthread -o lxmt ../lxlib/*.c ../lxlog/*.c ../lx_http/*.c *.c
