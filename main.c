#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include "lx_serror.h"
#include "lx_fileio.h"
#include "lx_http.h"
#include "lx_types.h"
#include "lx_mt_server.h"
#include "lx_daemon.h"

int main(int argc ,char * argv[])
{	
    lx_bool_t is_nostub = LX_FALSE,is_daemon = LX_FALSE ;
    int port = 8989,ret;
    char * host = NULL, *home = "home";
    char * help = "usage:lxmt [-h] [--port] [--home]";
    struct option opts [] ={
        {"help",0,NULL,'h'},
        
        {"daemon",0,NULL,'d'},

        {"port",1,NULL,'p'},

        {"home",1,NULL,'2'},
        
        {NULL,0,NULL,0}
    };
    
    while(1){
        char c ;
        int opt_index;
        
        c = getopt_long(argc,argv,"hp:",opts,&opt_index);
        if( c == -1)
            break;
        switch(c){
        case '?':
        case 'h':
        default :
            err_quit("%s",help);
       
        case 'p':
            port = atoi(optarg);
            break;
        case 'd':
            is_daemon = LX_TRUE;
            break;
        case '2':
            home = optarg;
            break;
        }   
    };
    
    if(is_daemon){
        if(lx_daemon()){
            printf("lx_daemon() error,%d:%s\n",errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        printf("run daemon mode\n");
    }
    printf("host:%s,port:%d,home:%s,daemon:%d\n",host,port,home,is_daemon);

    if(init_lxmt_server(is_nostub,home)){
        perror("init_lxmt_server error\n");
        return EXIT_FAILURE;
    }
    printf("init simple server ok\n");

    if(start_lxmt_server(port)){
        perror("start_lxmt_server error\n");
        ret = EXIT_FAILURE;
        goto end;
    }
    ret = EXIT_SUCCESS;

end:
    if(cleanup_lxmt_server()){
        perror("cleanup_lxmt_server error\n");
        return EXIT_FAILURE;
    }
 
    return ret; 
}

