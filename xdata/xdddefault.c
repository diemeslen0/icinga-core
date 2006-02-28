/*****************************************************************************
 *
 * XDDDEFAULT.C - Default scheduled downtime data routines for Nagios
 *
 * Copyright (c) 2001-2006 Ethan Galstad (nagios@nagios.org)
 * Last Modified:   02-27-2006
 *
 * License:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/


/*********** COMMON HEADER FILES ***********/

#include "../include/config.h"
#include "../include/common.h"
#include "../include/locations.h"
#include "../include/downtime.h"

#ifdef NSCORE
#include "../include/objects.h"
#include "../include/nagios.h"
#endif

#ifdef NSCGI
#include "../include/cgiutils.h"
#endif


/**** IMPLEMENTATION SPECIFIC HEADER FILES ****/
#include "xdddefault.h"



char xdddefault_downtime_file[MAX_FILENAME_LENGTH]="";
char xdddefault_temp_file[MAX_FILENAME_LENGTH]="";

#ifdef NSCORE
extern unsigned long next_downtime_id;
extern scheduled_downtime *scheduled_downtime_list;
extern char *macro_x[MACRO_X_COUNT];
#endif




/******************************************************************/
/***************** COMMON CONFIG INITIALIZATION  ******************/
/******************************************************************/

/* grab configuration information from appropriate config file(s) */
int xdddefault_grab_config_info(char *config_file){
	char *input=NULL;
	mmapfile *thefile;
#ifdef NSCGI
	char *input2=NULL;
	mmapfile *thefile2;
	char *temp_buffer;
#endif

	/*** CORE PASSES IN MAIN CONFIG FILE, CGIS PASS IN CGI CONFIG FILE! ***/

	/* initialize the location of the downtime and temp files */
	strncpy(xdddefault_downtime_file,DEFAULT_DOWNTIME_FILE,sizeof(xdddefault_downtime_file)-1);
	strncpy(xdddefault_temp_file,DEFAULT_TEMP_FILE,sizeof(xdddefault_temp_file)-1);
	xdddefault_downtime_file[sizeof(xdddefault_downtime_file)-1]='\x0';
	xdddefault_temp_file[sizeof(xdddefault_temp_file)-1]='\x0';

	/* open the config file for reading */
	if((thefile=mmap_fopen(config_file))==NULL)
		return ERROR;

	/* read in all lines from the config file */
	while(1){

		/* free memory */
		free(input);

		/* read the next line */
		if((input=mmap_fgets(thefile))==NULL)
			break;

		strip(input);

		/* skip blank lines and comments */
		if(input[0]=='#' || input[0]=='\x0')
			continue;

#ifdef NSCGI
		/* CGI needs to find and read contents of main config file, since it was passed the name of the CGI config file */
		if(strstr(input,"main_config_file")==input){

			temp_buffer=strtok(input,"=");
			temp_buffer=strtok(NULL,"\n");
			if(temp_buffer==NULL)
				continue;
			
			if((thefile2=mmap_fopen(temp_buffer))==NULL)
				continue;

			/* read in all lines from the main config file */
			while(1){

				/* free memory */
				free(input2);

				/* read the next line */
				if((input2=mmap_fgets(thefile2))==NULL)
					break;

				strip(input2);

				/* skip blank lines and comments */
				if(input2[0]=='#' || input2[0]=='\x0')
					continue;

				xdddefault_grab_config_directives(input2);
			        }

			/* close the file */
			mmap_fclose(thefile2);
		        }
#endif

#ifdef NSCORE
		/* core read variables directly from the main config file */
		xdddefault_grab_config_directives(input);
#endif
	        }

	/* close the file */
	mmap_fclose(thefile);

	/* we didn't find the downtime file */
	if(!strcmp(xdddefault_downtime_file,""))
		return ERROR;

	/* we didn't find the temp file */
	if(!strcmp(xdddefault_temp_file,""))
		return ERROR;

#ifdef NSCORE
	/* save the downtime data file macro */
	if(macro_x[MACRO_DOWNTIMEDATAFILE]!=NULL)
		free(macro_x[MACRO_DOWNTIMEDATAFILE]);
	macro_x[MACRO_DOWNTIMEDATAFILE]=(char *)strdup(xdddefault_downtime_file);
	if(macro_x[MACRO_DOWNTIMEDATAFILE]!=NULL)
		strip(macro_x[MACRO_DOWNTIMEDATAFILE]);
#endif

	return OK;
        }


void xdddefault_grab_config_directives(char *input_buffer){
	char *temp_buffer;

	/* downtime file definition */
	if((strstr(input_buffer,"downtime_file")==input_buffer) || (strstr(input_buffer,"xdddefault_downtime_file")==input_buffer)){
		temp_buffer=strtok(input_buffer,"=");
		temp_buffer=strtok(NULL,"\n");
		if(temp_buffer==NULL)
			return;
		strncpy(xdddefault_downtime_file,temp_buffer,sizeof(xdddefault_downtime_file)-1);
		xdddefault_downtime_file[sizeof(xdddefault_downtime_file)-1]='\x0';
	        }


	/* temp file definition */
	if((strstr(input_buffer,"temp_file")==input_buffer) || (strstr(input_buffer,"xdddefault_temp_file")==input_buffer)){
		temp_buffer=strtok(input_buffer,"=");
		temp_buffer=strtok(NULL,"\n");
		if(temp_buffer==NULL)
			return;
		strncpy(xdddefault_temp_file,temp_buffer,sizeof(xdddefault_temp_file)-1);
		xdddefault_temp_file[sizeof(xdddefault_temp_file)-1]='\x0';
	        }

	return;	
        }



#ifdef NSCORE


/******************************************************************/
/*********** DOWNTIME INITIALIZATION/CLEANUP FUNCTIONS ************/
/******************************************************************/


/* initialize downtime data */
int xdddefault_initialize_downtime_data(char *main_config_file){

	/* grab configuration information */
	if(xdddefault_grab_config_info(main_config_file)==ERROR)
		return ERROR;

	/* create downtime file if necessary */
	xdddefault_create_downtime_file();

	/* read downtime data into memory */
	xdddefault_read_downtime_data(main_config_file);

	/* clean up the old downtime data */
	xdddefault_validate_downtime_data();

	/* initialize next downtime id if necessary */
	if(next_downtime_id==0L)
		next_downtime_id=1;

	return OK;
        }



/* creates an empty downtime data file if one doesn't already exist */
int xdddefault_create_downtime_file(void){
	struct stat statbuf;

	/* bail out if file already exists */
	if(!stat(xdddefault_downtime_file,&statbuf))
		return OK;

	/* create an empty file */
	xdddefault_save_downtime_data();

	return OK;
        }



/* removes invalid and old downtime entries from the downtime file */
int xdddefault_validate_downtime_data(void){
	scheduled_downtime *temp_downtime;
	scheduled_downtime *next_downtime;
	int update_file=FALSE;
	int save=TRUE;

	/* remove stale downtimes */
	for(temp_downtime=scheduled_downtime_list;temp_downtime!=NULL;temp_downtime=next_downtime){

		next_downtime=temp_downtime->next;
		save=TRUE;

		/* delete downtimes with invalid host names */
		if(find_host(temp_downtime->host_name)==NULL)
			save=FALSE;

		/* delete downtimes with invalid service descriptions */
		if(temp_downtime->type==SERVICE_DOWNTIME && find_service(temp_downtime->host_name,temp_downtime->service_description)==NULL)
			save=FALSE;

		/* delete downtimes that have expired */
		if(temp_downtime->end_time<time(NULL))
			save=FALSE;

		/* delete the downtime */
		if(save==FALSE){
			update_file=TRUE;
			delete_downtime(temp_downtime->type,temp_downtime->downtime_id);
		        }
	        }

	/* remove triggered downtimes without valid parents */
	for(temp_downtime=scheduled_downtime_list;temp_downtime!=NULL;temp_downtime=next_downtime){

		next_downtime=temp_downtime->next;
		save=TRUE;

		if(temp_downtime->triggered_by==0)
			continue;

		if(find_host_downtime(temp_downtime->triggered_by)==NULL && find_service_downtime(temp_downtime->triggered_by)==NULL)
			save=FALSE;

		/* delete the downtime */
		if(save==FALSE){
			update_file=TRUE;
			delete_downtime(temp_downtime->type,temp_downtime->downtime_id);
		        }
	        }

	/* update downtime file */
	if(update_file==TRUE)
		xdddefault_save_downtime_data();

	/* find the new starting index for downtime id if its missing*/
	if(next_downtime_id==0L){
		for(temp_downtime=scheduled_downtime_list;temp_downtime!=NULL;temp_downtime=temp_downtime->next){
			if(temp_downtime->downtime_id>=next_downtime_id)
				next_downtime_id=temp_downtime->downtime_id+1;
		        }
	        }

	return OK;
        }



/* removes invalid and old downtime entries from the downtime file */
int xdddefault_cleanup_downtime_data(char *main_config_file){

	/* we don't need to do any cleanup... */
	return OK;
        }



/******************************************************************/
/************************ SAVE FUNCTIONS **************************/
/******************************************************************/

/* adds a new scheduled host downtime entry */
int xdddefault_add_new_host_downtime(char *host_name, time_t entry_time, char *author, char *comment, time_t start_time, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long *downtime_id){

	/* find the next valid downtime id */
	while(find_host_downtime(next_downtime_id)!=NULL)
		next_downtime_id++;

	/* add downtime to list in memory */
	add_host_downtime(host_name,entry_time,author,comment,start_time,end_time,fixed,triggered_by,duration,next_downtime_id);

	/* update downtime file */
	xdddefault_save_downtime_data();

	/* return the id for the downtime we are about to add (this happens in the main code) */
	if(downtime_id!=NULL)
		*downtime_id=next_downtime_id;

	/* increment the downtime id */
	next_downtime_id++;

	return OK;
        }



/* adds a new scheduled service downtime entry */
int xdddefault_add_new_service_downtime(char *host_name, char *service_description, time_t entry_time, char *author, char *comment, time_t start_time, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long *downtime_id){

	/* find the next valid downtime id */
	while(find_service_downtime(next_downtime_id)!=NULL)
		next_downtime_id++;

	/* add downtime to list in memory */
	add_service_downtime(host_name,service_description,entry_time,author,comment,start_time,end_time,fixed,triggered_by,duration,next_downtime_id);

	/* update downtime file */
	xdddefault_save_downtime_data();

	/* return the id for the downtime we are about to add (this happens in the main code) */
	if(downtime_id!=NULL)
		*downtime_id=next_downtime_id;

	/* increment the downtime id */
	next_downtime_id++;

	return OK;
        }


/******************************************************************/
/********************** DELETION FUNCTIONS ************************/
/******************************************************************/

/* deletes a scheduled host downtime entry */
int xdddefault_delete_host_downtime(unsigned long downtime_id){
	int result;

	result=xdddefault_delete_downtime(HOST_DOWNTIME,downtime_id);

	return result;
        }


/* deletes a scheduled service downtime entry */
int xdddefault_delete_service_downtime(unsigned long downtime_id){
	int result;

	result=xdddefault_delete_downtime(SERVICE_DOWNTIME,downtime_id);

	return result;
        }


/* deletes a scheduled host or service downtime entry */
int xdddefault_delete_downtime(int type, unsigned long downtime_id){

	/* rewrite the downtime file (downtime was already removed from memory) */
	xdddefault_save_downtime_data();

	return OK;
        }



/******************************************************************/
/****************** DOWNTIME OUTPUT FUNCTIONS *********************/
/******************************************************************/

/* writes downtime data to file */
int xdddefault_save_downtime_data(void){
	char temp_file[MAX_FILENAME_LENGTH];
	time_t current_time;
	scheduled_downtime *temp_downtime;
	int fd=0;
	FILE *fp=NULL;

	/* open a safe temp file for output */
	snprintf(temp_file,sizeof(temp_file)-1,"%sXXXXXX",xdddefault_temp_file);
	temp_file[sizeof(temp_file)-1]='\x0';
	if((fd=mkstemp(temp_file))==-1)
		return ERROR;
	fp=fdopen(fd,"w");
	if(fp==NULL){
		close(fd);
		unlink(temp_file);
		return ERROR;
	        }

	/* write header */
	fprintf(fp,"########################################\n");
	fprintf(fp,"#          NAGIOS DOWNTIME FILE\n");
	fprintf(fp,"#\n");
	fprintf(fp,"# THIS FILE IS AUTOMATICALLY GENERATED\n");
	fprintf(fp,"# BY NAGIOS.  DO NOT MODIFY THIS FILE!\n");
	fprintf(fp,"########################################\n\n");

	time(&current_time);

	/* write file info */
	fprintf(fp,"info {\n");
	fprintf(fp,"\tcreated=%lu\n",current_time);
	fprintf(fp,"\tversion=%s\n",PROGRAM_VERSION);
	fprintf(fp,"\t}\n\n");

	/* save all downtime */
	for(temp_downtime=scheduled_downtime_list;temp_downtime!=NULL;temp_downtime=temp_downtime->next){

		if(temp_downtime->type==HOST_DOWNTIME)
			fprintf(fp,"hostdowntime {\n");
		else
			fprintf(fp,"servicedowntime {\n");
		fprintf(fp,"\thost_name=%s\n",temp_downtime->host_name);
		if(temp_downtime->type==SERVICE_DOWNTIME)
			fprintf(fp,"\tservice_description=%s\n",temp_downtime->service_description);
		fprintf(fp,"\tdowntime_id=%lu\n",temp_downtime->downtime_id);
		fprintf(fp,"\tentry_time=%lu\n",temp_downtime->entry_time);
		fprintf(fp,"\tstart_time=%lu\n",temp_downtime->start_time);
		fprintf(fp,"\tend_time=%lu\n",temp_downtime->end_time);
		fprintf(fp,"\ttriggered_by=%lu\n",temp_downtime->triggered_by);
		fprintf(fp,"\tfixed=%d\n",temp_downtime->fixed);
		fprintf(fp,"\tduration=%lu\n",temp_downtime->duration);
		fprintf(fp,"\tauthor=%s\n",temp_downtime->author);
		fprintf(fp,"\tcomment=%s\n",temp_downtime->comment);
		fprintf(fp,"\t}\n\n");
	        }

	/* reset file permissions */
	fchmod(fd,S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

	/* close the temp file */
	fclose(fp);

	/* move the temp file to the downtime file (overwrite the old downtime file) */
	if(my_rename(temp_file,xdddefault_downtime_file))
		return ERROR;

	return OK;
        }


#endif




/******************************************************************/
/****************** DOWNTIME INPUT FUNCTIONS **********************/
/******************************************************************/


/* read the downtime file */
int xdddefault_read_downtime_data(char *main_config_file){
	char *input=NULL;
	int data_type=XDDDEFAULT_NO_DATA;
	char *var;
	char *val;
	mmapfile *thefile;
	unsigned long downtime_id=0;
	time_t entry_time=0L;
	time_t start_time=0L;
	time_t end_time=0L;
	int fixed=FALSE;
	unsigned long triggered_by=0;
	unsigned long duration=0L;
	char *host_name=NULL;
	char *service_description=NULL;
	char *comment=NULL;
	char *author=NULL;


#ifdef NSCGI
	/* grab configuration information */
	if(xdddefault_grab_config_info(main_config_file)==ERROR)
		return ERROR;
#endif

	/* open the downtime file */
	if((thefile=mmap_fopen(xdddefault_downtime_file))==NULL)
		return ERROR;

	while(1){

		/* free memory */
		free(input);

		/* get the next line */
		if((input=mmap_fgets(thefile))==NULL)
			break;

		strip(input);

		/* skip blank lines and comments */
		if(input[0]=='#' || input[0]=='\x0')
			continue;

		else if(!strcmp(input,"info {"))
			data_type=XDDDEFAULT_INFO_DATA;
		else if(!strcmp(input,"hostdowntime {"))
			data_type=XDDDEFAULT_HOST_DATA;
		else if(!strcmp(input,"servicedowntime {"))
			data_type=XDDDEFAULT_SERVICE_DATA;

		else if(!strcmp(input,"}")){

			switch(data_type){

			case XDDDEFAULT_INFO_DATA:
				break;

			case XDDDEFAULT_HOST_DATA:
			case XDDDEFAULT_SERVICE_DATA:

				/* add the downtime */
				if(data_type==XDDDEFAULT_HOST_DATA)
					add_host_downtime(host_name,entry_time,author,comment,start_time,end_time,fixed,triggered_by,duration,downtime_id);
				else
					add_service_downtime(host_name,service_description,entry_time,author,comment,start_time,end_time,fixed,triggered_by,duration,downtime_id);
#ifdef NSCORE
				/* must register the downtime with Nagios so it can schedule it, add comments, etc. */
				register_downtime((data_type==XDDDEFAULT_HOST_DATA)?HOST_DOWNTIME:SERVICE_DOWNTIME,downtime_id);
#endif
				break;

			default:
				break;
			        }

			data_type=XDDDEFAULT_NO_DATA;

			/* free temp memory */
			free(host_name);
			free(service_description);
			free(author);
			free(comment);

			/* reset defaults */
			host_name=NULL;
			service_description=NULL;
			author=NULL;
			comment=NULL;
			downtime_id=0;
			entry_time=0L;
			start_time=0L;
			end_time=0L;
			fixed=FALSE;
			triggered_by=0;
			duration=0L;
		        }

		else if(data_type!=XDDDEFAULT_NO_DATA){

			var=strtok(input,"=");
			val=strtok(NULL,"\n");
			if(val==NULL)
				continue;

			switch(data_type){

			case XDDDEFAULT_INFO_DATA:
				break;

			case XDDDEFAULT_HOST_DATA:
			case XDDDEFAULT_SERVICE_DATA:
				if(!strcmp(var,"host_name"))
					host_name=strdup(val);
				else if(!strcmp(var,"service_description"))
					service_description=strdup(val);
				else if(!strcmp(var,"downtime_id"))
					downtime_id=strtoul(val,NULL,10);
				else if(!strcmp(var,"entry_time"))
					entry_time=strtoul(val,NULL,10);
				else if(!strcmp(var,"start_time"))
					start_time=strtoul(val,NULL,10);
				else if(!strcmp(var,"end_time"))
					end_time=strtoul(val,NULL,10);
				else if(!strcmp(var,"fixed"))
					fixed=(atoi(val)>0)?TRUE:FALSE;
				else if(!strcmp(var,"triggered_by"))
					triggered_by=strtoul(val,NULL,10);
				else if(!strcmp(var,"duration"))
					duration=strtoul(val,NULL,10);
				else if(!strcmp(var,"author"))
					author=strdup(val);
				else if(!strcmp(var,"comment"))
					comment=strdup(val);
				break;

			default:
				break;
			        }

		        }
	        }

	/* free memory and close the file */
	free(input);
	mmap_fclose(thefile);

	return OK;
        }

