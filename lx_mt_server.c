#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include "lx_mt_server.h"
#include "lxtime.h"
#include "lx_http.h"
#include "lx_http_util.h"
#include "lxlog.h"

#define g_ctx (g_lxmt_server_ctx)

struct lxmt_hdarg
{
    lx_connection conn;
};
typedef struct lxmt_hdarg lxmt_hdarg; 

struct lxmt_server_ctx 
{
    int stimeo_milli;
    int rtimeo_milli;
    lx_bool_t is_nostub;
    struct lxlog log;
    struct lxlog_dailyas asarg;
};
typedef struct lxmt_server_ctx lxmt_server_ctx;

static lxmt_server_ctx *g_lxmt_server_ctx;

static char * g_home = "home";
static char * g_whome = "webhome";
static char * g_loghome = "logs";

int init_lxmt_server(lx_bool_t is_nostub,char * phome)
{
    char buff[1024];
    int ret = 0;
    struct lxmt_server_ctx * ctx;
    ctx = (lxmt_server_ctx *)malloc(sizeof(lxmt_server_ctx));
    if( ctx == NULL)
    {
        perror("malloc in init error\n");
        return -1;
    }
    ctx->stimeo_milli = 200000;
    ctx->rtimeo_milli = 200000;
    ctx->is_nostub = is_nostub;
   
    if(phome)
        g_home = phome;
  
    newlxlog( (&ctx->log));
    ctx->asarg.newhour = 18;
    ctx->log.arg = &ctx->asarg;
    if(snprintf(buff,1024,"%s/%s",g_home,g_loghome) <=0 || 
        (ret = lxlog_init(&ctx->log,buff,"access.log", LX_LOG_DEBUG)) ){
        printf("init log error,ret=%d",ret);
        return EXIT_FAILURE;
    }
    ctx->log.flushnow = 1;
    ctx->log.tlockflag = 1;
    ctx->log.plockflag = 0;
    ctx->log.showpid = 1;
   // ctx->log.showtid = 0;

    g_lxmt_server_ctx = ctx;

    return 0;
}

int cleanup_lxmt_server()
{
    if( g_ctx != NULL)
    {
        g_ctx->log.cleanup(&g_ctx->log);     
        free(&g_ctx); 
        g_ctx = NULL; 
    }    
    return 0;
}
/*just save request */
static int handler(void *);
static int do_work(void *);

int start_lxmt_server(int port)
{
    int listen_fd;
    lxmt_hdarg arg;
    char buff[1024];

    if( (listen_fd = lx_listen(port)) == -1)
    {
        perror("lx_listen error\n");
        return -1;
    }
    
    if( getwidetime(time(NULL),buff,1024) <= 0){
       perror("get start time error");
       return -1;
    }
    g_ctx->log.loginfo(&g_ctx->log,"server start at %s",buff);
   
    if(lx_start_server(listen_fd,handler,&arg ))
    {
        perror("lx_start_server error");
        return -1;
    }

    return 0;
}

static int recv_req(int fd, lx_bool_t is_nostub,h_parser_ctx * pctx);
static int send_resp(int fd, lx_bool_t is_nostub,h_parser_ctx * pctx);

static int handler(void * arg)
{
   int ret,qflag = 0;
   static int count = 0;
   pthread_t tid;
    
   lxmt_hdarg*  new_arg ;
   while( ( new_arg= (lxmt_hdarg*)malloc( sizeof(lxmt_hdarg)) )== NULL){
        g_ctx->log.logerror( &g_ctx->log, "malloc in handler error[%d:%s]",ret, strerror(ret));
        sleep(1);
   }
   memcpy(new_arg,arg,sizeof( lxmt_hdarg));

   while( ret = pthread_create(&tid,NULL,(void * (*)(void *))do_work,new_arg)){
        g_ctx->log.logwarn( &g_ctx->log, "pthread_create error[%d:%s]",ret, strerror(ret));
        sleep(1);
   }
   if( ret = pthread_detach(tid)){
        g_ctx->log.logerror( &g_ctx->log, "pthread_detach error[%d:%s]",ret, strerror(ret));
   }

   if(qflag  && count++ >= 3){
       sleep(30);
       return 1;
   }
   return 0;
}

static int do_work(void * arg)
{
    lxmt_hdarg *harg = (lxmt_hdarg*)arg;
    int fd,ret;
    char buff[1024];
    h_parser_ctx ctx;

    //printf("begin\n");
    
    fd = harg->conn.fd;
    if(lx_set_timeo(fd,g_ctx->stimeo_milli,g_ctx->rtimeo_milli))
    {
        g_ctx->log.logerror(&g_ctx->log,"lx_set_timeo error,fd:%d",fd);
        ret = -1;
        goto end;
    }
    
    http_set_uri_sep(&ctx,'?','&','=');
    http_set_memory_msuit(&ctx,malloc,free,http_extend);
 
	if( http_ctx_init(&ctx,T_REQ,64)){
        g_ctx->log.logerror(&g_ctx->log,"init parser ctx error");
        ret = -1;goto end;
    }

    if(recv_req(fd,g_ctx->is_nostub,&ctx)){
        g_ctx->log.logerror(&g_ctx->log ,"recv_req error,fd:%d",fd);
        ret = -1; goto end;
    }
/*
    if(http_print_http(&ctx)){
        g_ctx->log.logerror(&g_ctx->log,"print_pare_info error");
        ret = -1;goto end;
    }
*/
    if(send_resp(fd,g_ctx->is_nostub,&ctx)){
        g_ctx->log.logerror(&g_ctx->log,"send resp error,fd:%d",fd);
        ret = -1; goto end;
    }
    
    if( !inet_ntop(AF_INET, (void *)&harg->conn.addr.sin_addr, buff,16) ){
        g_ctx->log.logerror(&g_ctx->log,"inet_ntop error");
        ret = -1;goto end;
    }
    if(getwidetime(time(NULL),buff +16,32) <=0){
        g_ctx->log.logerror(&g_ctx->log,"get_wide_time error");
        ret = -1;goto end;
    }
    g_ctx->log.loginfo(&g_ctx->log,"uri:%s,addr:%s:%d,start:%s,duration:%ld"
        ,http_get_uri(&ctx.info),buff,(int)ntohs(harg->conn.addr.sin_port),
        buff+16,get_inval_micros(&harg->conn.accept_time ,NULL));
    ret = 0;
    
end:
    if(fd != -1){
        if(ret != 0)
            g_ctx->log.logerror(&g_ctx->log,"error occur in do_work, fd:%d ,ret:%d",fd,ret);
        close(fd);
        fd = -1;
        harg->conn.fd = -1;
    }   
    http_ctx_cleanup(&ctx);
    free (arg); 
    //printf("end\n");
    return ret;
}

static int recv_req(int fd,lx_bool_t is_nostub, h_parser_ctx * pctx)
{
    int ret,read_num;
    h_head_info * pinfo;

    FILE * fh = NULL;
    char *path = "request.stub";

    http_set_uri_sep(pctx,'?','&','=');
    http_set_memory_msuit(pctx,malloc,free,http_extend);
    pinfo  = &pctx->info;
    
    if(!is_nostub &&
        ( (fh = fopen(path,"wb")) == NULL) ){
        g_ctx->log.logerror(&g_ctx->log,"open stub file error:%s",path);
        ret = -1;goto err;
    }

	while(1)
	{
		read_num = recv(fd,lx_buffer_lenp((&pctx->orig_buff))
			,lx_buffer_freenum((&pctx->orig_buff)),0);
		if( read_num < 0){
            if(errno == EINTR )
                continue;
            ret = -1;
			g_ctx->log.logerror(&g_ctx->log,"recv error");goto err;
        }else if( read_num == 0){
			ret = -1;
			g_ctx->log.logerror(&g_ctx->log,"cannot get enough head info");goto err;
		}

		if(!is_nostub){
            if(fwriten(fh,lx_buffer_lenp( &pctx->orig_buff), read_num) != read_num){    
			    g_ctx->log.logerror(&g_ctx->log,"write stub file error"),ret =-1;goto err;
            }    
        }

        pctx->orig_buff.len += read_num;
		
		ret = http_parse(pctx);
		if( ret == HEC_OK){
			break;	
		}else if( ret == HEC_NEED_MORE)
			;
		else{
			g_ctx->log.logerror(&g_ctx->log,"parser error[%d]",ret);goto err;
			ret = -1;
		}
	}

    if(!is_nostub && http_save_body(fd, fh,"request_boby" ,pctx,LX_TRUE) ){
        g_ctx->log.logerror(&g_ctx->log,"save body error,%s","request_body");
        ret = -1;goto err;
    }

    ret = 0;
err:
    if(fh)
        fclose(fh);

    return ret;   
}

static int send_resp(int fd,lx_bool_t is_nostub, h_parser_ctx * req_ctx)
{
    int ret,head_len,contlen = -1, rcode;
    h_parser_ctx ctx,*pctx;
    char path[1024],date[64],buff[4096],* uri,*rstr;
    FILE * resp_fh = NULL,*stub_fh = NULL;
  
    char * headers[] = {  
       // "Date"          ,"Mon, 26 Jan 2015 08:52:12 GMT",
        "Content-Type" ,"text/html",
        "Connection"   ,"Keep-Alive",
        "Server"       ,"lanxin/spl 1.0",
        NULL,NULL
    };
    
    if( !is_nostub 
        &&(stub_fh = fopen("response.stub","wb")) ==NULL ){
        g_ctx->log.logerror(&g_ctx->log,"open response.stub file error");
        ret = -1; goto err;
    }
    
    http_set_uri_sep(&ctx,'?','&','=');
    http_set_memory_msuit(&ctx,malloc,free,http_extend);
    pctx = &ctx;
    
    if(http_ctx_init(&ctx,T_RESP,64)){
        g_ctx->log.logerror(&g_ctx->log,"http_ctx_init error");
        ret =-1;goto err;
    }
    
    if( !(ret = get_browser_time( time(NULL),date,64 ) ) ){
        g_ctx->log.logerror(&g_ctx->log,"snprintf date error");
        ret =-1;goto err;
    }

    uri = http_get_uri(&req_ctx->info);
    if( uri == NULL || strcmp(uri, "/") == 0)
        uri = "/index.html";
    if( (ret = snprintf(path,1024,"%s/%s%s",g_home,g_whome,uri)) <= 0 ){
        g_ctx->log.logerror(&g_ctx->log,"snprintf path error,ret = %d",ret);
        ret =-1;goto err;
    }
    
    if( (resp_fh = fopen(path, "rb")) == NULL){
        rcode = 404;
        rstr = "File Not Found";
        
        if( (ret = snprintf(path,1024,"%s/%s%s",g_home,g_whome,"/404.html")) <= 0 ){
            g_ctx->log.logerror(&g_ctx->log,"snprintf path error,ret = %d",ret);
            ret =-1;goto err;
        }
        if( (resp_fh= fopen(path,"rb"))== NULL){
            g_ctx->log.logerror(&g_ctx->log,"open 404 file error:%s",path);
            ret =-1;goto err;
        }
    }else{
        rcode = RESP_OK;
        rstr = NULL;
        if( fseek(resp_fh,0,SEEK_END)== -1 
            ||(ret = ftell(resp_fh)) == -1){
            g_ctx->log.logerror(&g_ctx->log,"get file len error");
            ret = -1; goto err;    
        }
        rewind(resp_fh);
        contlen = ret;
    }
    
    if( http_set_prot(pctx,P_HTTP_1_1))
    {
        g_ctx->log.logerror(&g_ctx->log,"set prot error");
        ret = -1;goto err;
    }

    if( http_set_rcode(pctx,rcode,rstr))
    {
        g_ctx->log.logerror(&g_ctx->log,"set resp code error");
        ret = -1;goto err;
    }

    if(http_set_headers(pctx,headers,contlen )){
        g_ctx->log.logerror(&g_ctx->log,"set headers error");
        ret = -1; goto err;
    }
    
    if(http_set_header(pctx,"Date",date )){
        g_ctx->log.logerror(&g_ctx->log,"set headers error");
        ret = -1; goto err;
    }

    if((head_len = http_seri_head(&pctx->info,T_RESP,buff,4096)) <= 0){
        g_ctx->log.logerror(&g_ctx->log,"http_ctx_serihead error");
        ret = -1;goto err;
    }
   
    if(lx_sosend(fd,buff,head_len)!=head_len
        || (!is_nostub && fwriten(stub_fh,buff,head_len) != head_len)){
        g_ctx->log.logerror(&g_ctx->log,"write or send  error");
        ret = -1;goto err;
    }

    while(resp_fh && (ret = freadn(resp_fh,buff,4096)) > 0 ){
        if( lx_sosend(fd,buff,ret)!= ret 
            || (!is_nostub && fwriten(stub_fh, buff,ret) != ret)){
            
            g_ctx->log.logerror(&g_ctx->log,"write or send response  error");
            ret = -1;goto err;
        }
    } 

    ret = 0;
err:
    http_ctx_cleanup(pctx);
    if(resp_fh)
        fclose(resp_fh);
    if(stub_fh)
        fclose(stub_fh);
    return ret;
}
