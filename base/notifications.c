/*****************************************************************************
 *
 * NOTIFICATIONS.C - Service and host notification functions for Nagios
 *
 * Copyright (c) 1999-2007 Ethan Galstad (nagios@nagios.org)
 * Last Modified:   05-26-2007
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

#include "../include/config.h"
#include "../include/common.h"
#include "../include/objects.h"
#include "../include/statusdata.h"
#include "../include/nagios.h"
#include "../include/broker.h"

extern notification    *notification_list;
extern contact         *contact_list;
extern serviceescalation *serviceescalation_list;
extern hostescalation  *hostescalation_list;

extern int             interval_length;
extern int             log_notifications;

extern int             enable_notifications;

extern int             notification_timeout;

extern unsigned long   next_notification_id;

extern char            *macro_x[MACRO_X_COUNT];

extern char            *generic_summary;



/******************************************************************/
/***************** SERVICE NOTIFICATION FUNCTIONS *****************/
/******************************************************************/


/* notify contacts about a service problem or recovery */
int service_notification(service *svc, int type, char *ack_author, char *ack_data){
	host *temp_host=NULL;
	notification *temp_notification=NULL;
	contact *temp_contact=NULL;
	time_t current_time;
	struct timeval start_time;
	struct timeval end_time;
	int escalated=FALSE;
	int result=OK;
	int contacts_notified=0;

	log_debug_info(DEBUGL_FUNCTIONS,0,"service_notification()\n");

	/* get the current time */
	time(&current_time);
	gettimeofday(&start_time,NULL);

	log_debug_info(DEBUGL_NOTIFICATIONS,0,"** Service Notification Attempt ** Host: '%s', Service: '%s', Type: %d, Current State: %d, Last Notification: %s",svc->host_name,svc->description,type,svc->current_state,ctime(&svc->last_notification));

	/* if we couldn't find the host, return an error */
	if((temp_host=svc->host_ptr)==NULL){
		log_debug_info(DEBUGL_NOTIFICATIONS,0,"Couldn't find the host associated with this service, so we won't send a notification!\n");
		return ERROR;
	        }

	/* check the viability of sending out a service notification */
	if(check_service_notification_viability(svc,type)==ERROR){
		log_debug_info(DEBUGL_NOTIFICATIONS,0,"Notification viability test failed.  No notification will be sent out.\n");
		return OK;
	        }

	log_debug_info(DEBUGL_NOTIFICATIONS,0,"Notification viability test passed.\n");

	/* if this is just a normal notification... */
	if(type==NOTIFICATION_NORMAL){

		/* increase the current notification number */
		svc->current_notification_number++;

		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Current notification number: %d\n",svc->current_notification_number);
	        }

	/* save and increase the current notification id */
	svc->current_notification_id=next_notification_id;
	next_notification_id++;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Creating list of contacts to be notified.\n");

	/* create the contact notification list for this service */
	create_notification_list_from_service(svc,&escalated);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	end_time.tv_sec=0L;
	end_time.tv_usec=0L;
	broker_notification_data(NEBTYPE_NOTIFICATION_START,NEBFLAG_NONE,NEBATTR_NONE,SERVICE_NOTIFICATION,type,start_time,end_time,(void *)svc,ack_author,ack_data,escalated,0,NULL);
#endif

	/* we have contacts to notify... */
	if(notification_list!=NULL){

		/* grab the macro variables */
		clear_volatile_macros();
		grab_host_macros(temp_host);
		grab_service_macros(svc);
		grab_datetime_macros();

		/* if this is an acknowledgement, get the acknowledgement macros */
		if(type==NOTIFICATION_ACKNOWLEDGEMENT){

			my_free((void **)&macro_x[MACRO_SERVICEACKAUTHOR]);
			if(ack_author)
				macro_x[MACRO_SERVICEACKAUTHOR]=(char *)strdup(ack_author);

			my_free((void **)macro_x[MACRO_SERVICEACKCOMMENT]);
			if(ack_data)
				macro_x[MACRO_SERVICEACKCOMMENT]=(char *)strdup(ack_data);

			/* see if we can find the contact */
			if((temp_contact=find_contact(ack_author))==NULL){
				for(temp_contact=contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){
					if(!strcmp(temp_contact->alias,ack_author))
						break;
					}
				}

			my_free((void **)&macro_x[MACRO_SERVICEACKAUTHORNAME]);
			my_free((void **)&macro_x[MACRO_SERVICEACKAUTHORALIAS]);
			if(temp_contact!=NULL){
				macro_x[MACRO_SERVICEACKAUTHORNAME]=(char *)strdup(temp_contact->name);
				macro_x[MACRO_SERVICEACKAUTHORALIAS]=(char *)strdup(temp_contact->alias);
				}
	                }

		/* set the notification type macro */
		my_free((void **)&macro_x[MACRO_NOTIFICATIONTYPE]);
		if(type==NOTIFICATION_ACKNOWLEDGEMENT)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("ACKNOWLEDGEMENT");
		else if(type==NOTIFICATION_FLAPPINGSTART)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("FLAPPINGSTART");
		else if(type==NOTIFICATION_FLAPPINGSTOP)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("FLAPPINGSTOP");
		else if(type==NOTIFICATION_FLAPPINGDISABLED)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("FLAPPINGDISABLED");
		else if(type==NOTIFICATION_DOWNTIMESTART)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("DOWNTIMESTART");
		else if(type==NOTIFICATION_DOWNTIMEEND)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("DOWNTIMEEND");
		else if(type==NOTIFICATION_DOWNTIMECANCELLED)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("DOWNTIMECANCELLED");
		else if(svc->current_state==STATE_OK)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("RECOVERY");
		else
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("PROBLEM");

		/* set the notification number macro */
		my_free((void **)&macro_x[MACRO_SERVICENOTIFICATIONNUMBER]);
		asprintf(&macro_x[MACRO_SERVICENOTIFICATIONNUMBER],"%d",svc->current_notification_number);

		/* the $NOTIFICATIONNUMBER$ macro is maintained for backward compatability */
		my_free((void **)&macro_x[MACRO_NOTIFICATIONNUMBER]);
		macro_x[MACRO_NOTIFICATIONNUMBER]=(char *)strdup((macro_x[MACRO_SERVICENOTIFICATIONNUMBER]==NULL)?"":macro_x[MACRO_SERVICENOTIFICATIONNUMBER]);

		/* set the notification id macro */
		my_free((void **)&macro_x[MACRO_SERVICENOTIFICATIONID]);
		asprintf(&macro_x[MACRO_SERVICENOTIFICATIONID],"%lu",svc->current_notification_id);

		/* notify each contact (duplicates have been removed) */
		for(temp_notification=notification_list;temp_notification!=NULL;temp_notification=temp_notification->next){

			/* grab the macro variables for this contact */
			grab_contact_macros(temp_notification->contact);

			/* grab summary macros (customized for this contact) */
			grab_summary_macros(temp_notification->contact);

			/* notify this contact */
			result=notify_contact_of_service(temp_notification->contact,svc,type,ack_author,ack_data,escalated);

			/* keep track of how many contacts were notified */
			if(result==OK)
				contacts_notified++;
		        }

		/* free memory allocated to the notification list */
		free_notification_list();

		if(type==NOTIFICATION_NORMAL){

			/* adjust last/next notification time and notification flags if we notified someone */
			if(contacts_notified>0){

				/* calculate the next acceptable re-notification time */
				svc->next_notification=get_next_service_notification_time(svc,current_time);

				log_debug_info(DEBUGL_NOTIFICATIONS,0,"No contacts were notified.  Next possible notification time: %s",ctime(&svc->next_notification));

				/* update the last notification time for this service (this is needed for rescheduling later notifications) */
				svc->last_notification=current_time;

				/* update notifications flags */
				if(svc->current_state==STATE_UNKNOWN)
					svc->notified_on_unknown=TRUE;
				else if(svc->current_state==STATE_WARNING)
					svc->notified_on_warning=TRUE;
				else if(svc->current_state==STATE_CRITICAL)
					svc->notified_on_critical=TRUE;
			        }

			/* we didn't end up notifying anyone, so adjust current notification number */
			else
				svc->current_notification_number--;
		        }

		log_debug_info(DEBUGL_NOTIFICATIONS,0,"%d contacts were notified.",contacts_notified);
	        }

	/* there were no contacts, so no notification really occurred... */
	else{

		/* readjust current notification number, since one didn't go out */
		if(type==NOTIFICATION_NORMAL)
			svc->current_notification_number--;

		log_debug_info(DEBUGL_NOTIFICATIONS,0,"No contacts were found for notification purposes.  No notification was sent out.\n");
	        }

	/* get the time we finished */
	gettimeofday(&end_time,NULL);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_notification_data(NEBTYPE_NOTIFICATION_END,NEBFLAG_NONE,NEBATTR_NONE,SERVICE_NOTIFICATION,type,start_time,end_time,(void *)svc,ack_author,ack_data,escalated,contacts_notified,NULL);
#endif

	/* update the status log with the service information */
	update_service_status(svc,FALSE);

	return OK;
	}



/* checks the viability of sending out a service alert (top level filters) */
int check_service_notification_viability(service *svc, int type){
	host *temp_host;
	time_t current_time;
	time_t timeperiod_start;
	
	log_debug_info(DEBUGL_FUNCTIONS,0,"check_service_notification_viability()\n");

	/* get current time */
	time(&current_time);

	/* are notifications enabled? */
	if(enable_notifications==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Notifications are disabled, so service notifications (problems and acknowledments) will not be sent out.\n");
		return ERROR;
	        }

	/* find the host this service is associated with */
	temp_host=find_host(svc->host_name);

	/* if we couldn't find the host, return an error */
	if(temp_host==NULL){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Couldn't find the host associated with this service, so we won't send a notification.\n");
		return ERROR;
	        }

	/* see if the service can have notifications sent out at this time */
	if(check_time_against_period(current_time,svc->notification_period_ptr)==ERROR){

		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This service shouldn't have notifications sent out at this time.\n");

		/* calculate the next acceptable notification time, once the next valid time range arrives... */
		if(type==NOTIFICATION_NORMAL){

			get_next_valid_time(current_time,&timeperiod_start,svc->notification_period_ptr);

			/* looks like there are no valid notification times defined, so schedule the next one far into the future (one year)... */
			if(timeperiod_start==(time_t)0)
				svc->next_notification=(time_t)(current_time+(60*60*24*365));

			/* else use the next valid notification time */
			else
				svc->next_notification=timeperiod_start;

			log_debug_info(DEBUGL_NOTIFICATIONS,1,"Next possible notification time: %s\n",ctime(&svc->next_notification));
		        }

		return ERROR;
	        }

	/* are notifications temporarily disabled for this service? */
	if(svc->notifications_enabled==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Notifications are temporarily disabled for this service, so we won't send one out.\n");
		return ERROR;
	        }


	/****************************************/
	/*** SPECIAL CASE FOR ACKNOWLEGEMENTS ***/
	/****************************************/

	/* acknowledgements only have to pass three general filters, although they have another test of their own... */
	if(type==NOTIFICATION_ACKNOWLEDGEMENT){

		/* don't send an acknowledgement if there isn't a problem... */
		if(svc->current_state==STATE_OK){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"The service is currently OK, so we won't send an acknowledgement.\n");
			return ERROR;
	                }

		/* acknowledgement viability test passed, so the notification can be sent out */
		return OK;
	        }


	/****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/****************************************/

	/* flapping notifications only have to pass three general filters */
	if(type==NOTIFICATION_FLAPPINGSTART || type==NOTIFICATION_FLAPPINGSTOP || type==NOTIFICATION_FLAPPINGDISABLED){

		/* don't send a notification if we're not supposed to... */
		if(svc->notify_on_flapping==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about FLAPPING events for this service.\n");
			return ERROR;
	                }

		/* don't send notifications during scheduled downtime */
		if(svc->scheduled_downtime_depth>0 || temp_host->scheduled_downtime_depth>0){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about FLAPPING events during scheduled downtime.\n");
			return ERROR;
		        }

		/* flapping viability test passed, so the notification can be sent out */
		return OK;
	        }


	/****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/****************************************/

	/* downtime notifications only have to pass three general filters */
	if(type==NOTIFICATION_DOWNTIMESTART || type==NOTIFICATION_DOWNTIMEEND || type==NOTIFICATION_DOWNTIMEEND){

		/* don't send a notification if we're not supposed to... */
		if(svc->notify_on_downtime==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about DOWNTIME events for this service.\n");
			return ERROR;
	                }

		/* don't send notifications during scheduled downtime (for service only, not host) */
		if(svc->scheduled_downtime_depth>0){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about DOWNTIME events during scheduled downtime.\n");
			return ERROR;
		        }

		/* downtime viability test passed, so the notification can be sent out */
		return OK;
	        }


	/****************************************/
	/*** NORMAL NOTIFICATIONS ***************/
	/****************************************/

	/* is this a hard problem/recovery? */
	if(svc->state_type==SOFT_STATE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This service is in a soft state, so we won't send a notification out.\n");
		return ERROR;
	        }

	/* has this problem already been acknowledged? */
	if(svc->problem_has_been_acknowledged==TRUE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This service problem has already been acknowledged, so we won't send a notification out.\n");
		return ERROR;
	        }

	/* check service notification dependencies */
	if(check_service_dependencies(svc,NOTIFICATION_DEPENDENCY)==DEPENDENCIES_FAILED){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Service notification dependencies for this service have failed, so we won't sent a notification out.\n");
		return ERROR;
	        }

	/* check host notification dependencies */
	if(check_host_dependencies(temp_host,NOTIFICATION_DEPENDENCY)==DEPENDENCIES_FAILED){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Host notification dependencies for this service have failed, so we won't sent a notification out.\n");
		return ERROR;
	        }

	/* see if we should notify about problems with this service */
	if(svc->current_state==STATE_UNKNOWN && svc->notify_on_unknown==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about UNKNOWN states for this service.\n");
		return ERROR;
	        }
	if(svc->current_state==STATE_WARNING && svc->notify_on_warning==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about WARNING states for this service.\n");
		return ERROR;
	        }
	if(svc->current_state==STATE_CRITICAL && svc->notify_on_critical==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about CRITICAL states for this service.\n");
		return ERROR;
	        }
	if(svc->current_state==STATE_OK){
		if(svc->notify_on_recovery==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about RECOVERY states for this service.\n");
			return ERROR;
		        }
		if(!(svc->notified_on_unknown==TRUE || svc->notified_on_warning==TRUE || svc->notified_on_critical==TRUE)){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about this recovery.\n");
			return ERROR;
		        }
	        }

	/* see if enough time has elapsed for first notification (Mathias Sundman) */
	if(current_time < (time_t)(svc->last_hard_state_change + (svc->first_notification_delay*interval_length))){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Enough time has not elapsed since the host since the service changed state, so we should not notify about this yet\n");
		return ERROR;
	        }

	/* if this service is currently flapping, don't send the notification */
	if(svc->is_flapping==TRUE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This service is currently flapping, so we won't send notifications.\n");
		return ERROR;
	        }

	/***** RECOVERY NOTIFICATIONS ARE GOOD TO GO AT THIS POINT *****/
	if(svc->current_state==STATE_OK)
		return OK;

	/* don't notify contacts about this service problem again if the notification interval is set to 0 */
	if(svc->no_more_notifications==TRUE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't re-notify contacts about this service problem.\n");
		return ERROR;
	        }

	/* if the host is down or unreachable, don't notify contacts about service failures */
	if(temp_host->current_state!=HOST_UP){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"The host is either down or unreachable, so we won't notify contacts about this service.\n");
		return ERROR;
	        }
	
	/* don't notify if we haven't waited long enough since the last time (and the service is not marked as being volatile) */
	if((current_time < svc->next_notification) && svc->is_volatile==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We haven't waited long enough to re-notify contacts about this service.\n");
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Next valid notification time: %s",ctime(&svc->next_notification));
		return ERROR;
	        }

	/* if this service is currently in a scheduled downtime period, don't send the notification */
	if(svc->scheduled_downtime_depth>0){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This service is currently in a scheduled downtime, so we won't send notifications.\n");
		return ERROR;
	        }

	/* if this host is currently in a scheduled downtime period, don't send the notification */
	if(temp_host->scheduled_downtime_depth>0){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"The host this service is associated with is currently in a scheduled downtime, so we won't send notifications.\n");
		return ERROR;
	        }

	return OK;
        }



/* check viability of sending out a service notification to a specific contact (contact-specific filters) */
int check_contact_service_notification_viability(contact *cntct, service *svc, int type){

	log_debug_info(DEBUGL_FUNCTIONS,0,"check_contact_service_notification_viability()\n");

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Checking service notification viability for contact '%s'...\n",cntct->name);

	/* are notifications enabled? */
	if(cntct->service_notifications_enabled==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"Service notifications are disabled for this contact.\n");
		return ERROR;
	        }

	/* see if the contact can be notified at this time */
	if(check_time_against_period(time(NULL),cntct->service_notification_period_ptr)==ERROR){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"This contact shouldn't be notified at this time.\n");
		return ERROR;
	        }

	/****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/****************************************/

	if(type==NOTIFICATION_FLAPPINGSTART || type==NOTIFICATION_FLAPPINGSTOP || type==NOTIFICATION_FLAPPINGDISABLED){

		if(cntct->notify_on_service_flapping==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about FLAPPING service events.\n");
			return ERROR;
		        }

		return OK;
	        }

	/****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/****************************************/

	if(type==NOTIFICATION_DOWNTIMESTART || type==NOTIFICATION_DOWNTIMEEND || type==NOTIFICATION_DOWNTIMECANCELLED){

		if(cntct->notify_on_service_downtime==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about DOWNTIME service events.\n");
			return ERROR;
		        }

		return OK;
	        }

	/*************************************/
	/*** ACKS AND NORMAL NOTIFICATIONS ***/
	/*************************************/

	/* see if we should notify about problems with this service */
	if(svc->current_state==STATE_UNKNOWN && cntct->notify_on_service_unknown==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about UNKNOWN service states.\n");
		return ERROR;
	        }

	if(svc->current_state==STATE_WARNING && cntct->notify_on_service_warning==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about WARNING service states.\n");
		return ERROR;
	        }

	if(svc->current_state==STATE_CRITICAL && cntct->notify_on_service_critical==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about CRITICAL service states.\n");
		return ERROR;
	        }

	if(svc->current_state==STATE_OK){

		if(cntct->notify_on_service_recovery==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about RECOVERY service states.\n");
			return ERROR;
		        }

		if(!((svc->notified_on_unknown==TRUE && cntct->notify_on_service_unknown==TRUE) || (svc->notified_on_warning==TRUE && cntct->notify_on_service_warning==TRUE) || (svc->notified_on_critical==TRUE && cntct->notify_on_service_critical==TRUE))){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify about this recovery.\n");
			return ERROR;
		        }
	        }

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Service notification viability for contact '%s' PASSED.\n",cntct->name);

	return OK;
        }


/* notify a specific contact about a service problem or recovery */
int notify_contact_of_service(contact *cntct, service *svc, int type, char *ack_author, char *ack_data, int escalated){
	commandsmember *temp_commandsmember;
	char command_name[MAX_INPUT_BUFFER];
	char *command_name_ptr=NULL;
	char raw_command[MAX_COMMAND_BUFFER];
	char processed_command[MAX_COMMAND_BUFFER];
	char *temp_buffer=NULL;
	int early_timeout=FALSE;
	double exectime;
	struct timeval start_time,end_time;
	struct timeval method_start_time,method_end_time;
	int macro_options=STRIP_ILLEGAL_MACRO_CHARS|ESCAPE_MACRO_CHARS;


	log_debug_info(DEBUGL_FUNCTIONS,0,"notify_contact_of_service()\n");
	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Attempting to notifying contact '%s'...\n",cntct->name);

	/* check viability of notifying this user */
	/* acknowledgements are no longer excluded from this test - added 8/19/02 Tom Bertelson */
	if(check_contact_service_notification_viability(cntct,svc,type)==ERROR)
		return ERROR;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Notifying contact '%s'\n",cntct->name);

	/* get start time */
	gettimeofday(&start_time,NULL);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	end_time.tv_sec=0L;
	end_time.tv_usec=0L;
	broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_START,NEBFLAG_NONE,NEBATTR_NONE,SERVICE_NOTIFICATION,type,start_time,end_time,(void *)svc,cntct,ack_author,ack_data,escalated,NULL);
#endif

	/* process all the notification commands this user has */
	for(temp_commandsmember=cntct->service_notification_commands;temp_commandsmember!=NULL;temp_commandsmember=temp_commandsmember->next){

		/* get start time */
		gettimeofday(&method_start_time,NULL);

#ifdef USE_EVENT_BROKER
		/* send data to event broker */
		method_end_time.tv_sec=0L;
		method_end_time.tv_usec=0L;
		broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_START,NEBFLAG_NONE,NEBATTR_NONE,SERVICE_NOTIFICATION,type,method_start_time,method_end_time,(void *)svc,cntct,temp_commandsmember->command,ack_author,ack_data,escalated,NULL);
#endif

		/* get the command name */
		strncpy(command_name,temp_commandsmember->command,sizeof(command_name));
		command_name[sizeof(command_name)-1]='\x0';
		command_name_ptr=strtok(command_name,"!");

		/* get the raw command line */
		get_raw_command_line(temp_commandsmember->command_ptr,temp_commandsmember->command,raw_command,sizeof(raw_command),macro_options);

		/* process any macros contained in the argument */
		process_macros(raw_command,processed_command,sizeof(processed_command),macro_options);

		/* run the notification command */
		if(strcmp(processed_command,"")){

			log_debug_info(DEBUGL_NOTIFICATIONS,2,"Raw notification command: %s\n",raw_command);
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"Processed notification command: %s\n",processed_command);

			/* log the notification to program log file */
			if(log_notifications==TRUE){
				switch(type){
				case NOTIFICATION_ACKNOWLEDGEMENT:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;ACKNOWLEDGEMENT (%s);%s;%s;%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT],macro_x[MACRO_SERVICEACKAUTHOR],macro_x[MACRO_SERVICEACKCOMMENT]);
					break;
				case NOTIFICATION_FLAPPINGSTART:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;FLAPPINGSTART (%s);%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT]);
					break;
				case NOTIFICATION_FLAPPINGSTOP:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;FLAPPINGSTOP (%s);%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT]);
					break;
				case NOTIFICATION_FLAPPINGDISABLED:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;FLAPPINGDISABLED (%s);%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT]);
					break;
				case NOTIFICATION_DOWNTIMESTART:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;DOWNTIMESTART (%s);%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT]);
					break;
				case NOTIFICATION_DOWNTIMEEND:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;DOWNTIMEEND (%s);%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT]);
					break;
				case NOTIFICATION_DOWNTIMECANCELLED:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;DOWNTIMECANCELLED (%s);%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT]);
					break;
				default:
					asprintf(&temp_buffer,"SERVICE NOTIFICATION: %s;%s;%s;%s;%s;%s\n",cntct->name,svc->host_name,svc->description,macro_x[MACRO_SERVICESTATE],command_name_ptr,macro_x[MACRO_SERVICEOUTPUT]);
					break;
				        }
				write_to_all_logs(temp_buffer,NSLOG_SERVICE_NOTIFICATION);
				my_free((void **)&temp_buffer);
			        }

			/* run the command */
			my_system(processed_command,notification_timeout,&early_timeout,&exectime,NULL,0);

			/* check to see if the notification command timed out */
			if(early_timeout==TRUE){
				asprintf(&temp_buffer,"Warning: Contact '%s' service notification command '%s' timed out after %d seconds\n",cntct->name,processed_command,DEFAULT_NOTIFICATION_TIMEOUT);
				write_to_logs_and_console(temp_buffer,NSLOG_SERVICE_NOTIFICATION | NSLOG_RUNTIME_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
		        }

		/* get end time */
		gettimeofday(&method_end_time,NULL);

#ifdef USE_EVENT_BROKER
		/* send data to event broker */
		broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_END,NEBFLAG_NONE,NEBATTR_NONE,SERVICE_NOTIFICATION,type,method_start_time,method_end_time,(void *)svc,cntct,temp_commandsmember->command,ack_author,ack_data,escalated,NULL);
#endif
	        }

	/* get end time */
	gettimeofday(&end_time,NULL);

	/* update the contact's last service notification time */
	cntct->last_service_notification=start_time.tv_sec;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_END,NEBFLAG_NONE,NEBATTR_NONE,SERVICE_NOTIFICATION,type,start_time,end_time,(void *)svc,cntct,ack_author,ack_data,escalated,NULL);
#endif

	return OK;
        }


/* checks to see if a service escalation entry is a match for the current service notification */
int is_valid_escalation_for_service_notification(service *svc,serviceescalation *se){
	int notification_number=0;
	time_t current_time=0L;
	service *temp_service=NULL;

	log_debug_info(DEBUGL_FUNCTIONS,0,"is_valid_escalation_for_service_notification()\n");

	/* get the current time */
	time(&current_time);

	/* if this is a recovery, really we check for who got notified about a previous problem */
	if(svc->current_state==STATE_OK)
		notification_number=svc->current_notification_number-1;
	else
		notification_number=svc->current_notification_number;

	/* this entry if it is not for this service */
	temp_service=se->service_ptr;
	if(temp_service==NULL || temp_service!=svc)
		return FALSE;

	/* skip this escalation if it happens later */
	if(se->first_notification > notification_number)
		return FALSE;

	/* skip this escalation if it has already passed */
	if(se->last_notification!=0 && se->last_notification < notification_number)
		return FALSE;

	/* skip this escalation if it has a timeperiod and the current time isn't valid */
	if(se->escalation_period!=NULL && check_time_against_period(current_time,se->escalation_period_ptr)==ERROR)
		return FALSE;

	/* skip this escalation if the state options don't match */
	if(svc->current_state==STATE_OK && se->escalate_on_recovery==FALSE)
		return FALSE;
	else if(svc->current_state==STATE_WARNING && se->escalate_on_warning==FALSE)
		return FALSE;
	else if(svc->current_state==STATE_UNKNOWN && se->escalate_on_unknown==FALSE)
		return FALSE;
	else if(svc->current_state==STATE_CRITICAL && se->escalate_on_critical==FALSE)
		return FALSE;

	return TRUE;
        }


/* checks to see whether a service notification should be escalation */
int should_service_notification_be_escalated(service *svc){
	serviceescalation *temp_se=NULL;

	log_debug_info(DEBUGL_FUNCTIONS,0,"should_service_notification_be_escalated()\n");

	/* search the service escalation list */
	for(temp_se=serviceescalation_list;temp_se!=NULL;temp_se=temp_se->next){

		/* we found a matching entry, so escalate this notification! */
		if(is_valid_escalation_for_service_notification(svc,temp_se)==TRUE){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"Service notification WILL be escalated.\n");
			return TRUE;
		        }
	        }

	log_debug_info(DEBUGL_NOTIFICATIONS,1,"Service notification will NOT be escalated.\n");

	return FALSE;
        }


/* given a service, create a list of contacts to be notified, removing duplicates */
int create_notification_list_from_service(service *svc, int *escalated){
	serviceescalation *temp_se=NULL;
	contactsmember *temp_contactsmember=NULL;
	contact *temp_contact=NULL;

	log_debug_info(DEBUGL_FUNCTIONS,0,"create_notification_list_from_service()\n");

	/* should this notification be escalated? */
	if(should_service_notification_be_escalated(svc)==TRUE){

		/* set the escalation flag */
		*escalated=TRUE;

		/* search all the escalation entries for valid matches */
		for(temp_se=serviceescalation_list;temp_se!=NULL;temp_se=temp_se->next){

			/* skip this entry if it isn't appropriate */
			if(is_valid_escalation_for_service_notification(svc,temp_se)==FALSE)
				continue;

			log_debug_info(DEBUGL_NOTIFICATIONS,1,"Adding contacts from service escalation to notification list.\n");

			/* find each contact in this escalation entry */
			for(temp_contactsmember=temp_se->contacts;temp_contactsmember!=NULL;temp_contactsmember=temp_contactsmember->next){
				if((temp_contact=temp_contactsmember->contact_ptr)==NULL)
					continue;
				add_notification(temp_contact);
				}
		        }
	        }

	/* no escalation is necessary - just continue normally... */
	else{

		/* set the escalation flag */
		*escalated=FALSE;

		/* find all contacts for this service */
		for(temp_contactsmember=svc->contacts;temp_contactsmember!=NULL;temp_contactsmember=temp_contactsmember->next){
			if((temp_contact=temp_contactsmember->contact_ptr)==NULL)
				continue;
			add_notification(temp_contact);
			}
	        }

	return OK;
        }




/******************************************************************/
/******************* HOST NOTIFICATION FUNCTIONS ******************/
/******************************************************************/



/* notify all contacts for a host that the entire host is down or up */
int host_notification(host *hst, int type, char *ack_author, char *ack_data){
	notification *temp_notification=NULL;
	contact *temp_contact=NULL;
	time_t current_time;
	struct timeval start_time;
	struct timeval end_time;
	int escalated=FALSE;
	int result=OK;
	int contacts_notified=0;

	/* get the current time */
	time(&current_time);
	gettimeofday(&start_time,NULL);

	log_debug_info(DEBUGL_NOTIFICATIONS,0,"** Host Notification Attempt ** Host: '%s', Type: %d, Current State: %d, Last Notification: %s",hst->name,type,hst->current_state,ctime(&hst->last_host_notification));


	/* check viability of sending out a host notification */
	if(check_host_notification_viability(hst,type)==ERROR){
		log_debug_info(DEBUGL_NOTIFICATIONS,0,"Notification viability test failed.  No notification will be sent out.\n");
		return OK;
	        }

	log_debug_info(DEBUGL_NOTIFICATIONS,0,"Notification viability test passed.\n");

	/* if this is just a normal notification... */
	if(type==NOTIFICATION_NORMAL){

		/* increment the current notification number */
		hst->current_notification_number++;

		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Current notification number: %d\n",hst->current_notification_number);
	        }

	/* save and increase the current notification id */
	hst->current_notification_id=next_notification_id;
	next_notification_id++;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Creating list of contacts to be notified.\n");

	/* create the contact notification list for this host */
	create_notification_list_from_host(hst,&escalated);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	end_time.tv_sec=0L;
	end_time.tv_usec=0L;
	broker_notification_data(NEBTYPE_NOTIFICATION_START,NEBFLAG_NONE,NEBATTR_NONE,HOST_NOTIFICATION,type,start_time,end_time,(void *)hst,ack_author,ack_data,escalated,0,NULL);
#endif

	/* there are contacts to be notified... */
	if(notification_list!=NULL){

		/* grab the macro variables */
		clear_volatile_macros();
		grab_host_macros(hst);
		grab_datetime_macros();

		/* if this is an acknowledgement, get the acknowledgement macros */
		if(type==NOTIFICATION_ACKNOWLEDGEMENT){

			my_free((void **)&macro_x[MACRO_HOSTACKAUTHOR]);
			if(ack_author)
				macro_x[MACRO_HOSTACKAUTHOR]=(char *)strdup(ack_author);

			my_free((void **)&macro_x[MACRO_HOSTACKCOMMENT]);
			if(ack_data)
				macro_x[MACRO_HOSTACKCOMMENT]=(char *)strdup(ack_data);

			/* see if we can find the contact */
			if((temp_contact=find_contact(ack_author))==NULL){
				for(temp_contact=contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){
					if(!strcmp(temp_contact->alias,ack_author))
						break;
					}
				}

			my_free((void **)&macro_x[MACRO_SERVICEACKAUTHORNAME]);
			my_free((void **)&macro_x[MACRO_SERVICEACKAUTHORALIAS]);
			if(temp_contact!=NULL){
				macro_x[MACRO_SERVICEACKAUTHORNAME]=(char *)strdup(temp_contact->name);
				macro_x[MACRO_SERVICEACKAUTHORALIAS]=(char *)strdup(temp_contact->alias);
				}
	                }

		/* set the notification type macro */
		my_free((void **)&macro_x[MACRO_NOTIFICATIONTYPE]);
		if(type==NOTIFICATION_ACKNOWLEDGEMENT)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("ACKNOWLEDGEMENT");
		else if(type==NOTIFICATION_FLAPPINGSTART)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("FLAPPINGSTART");
		else if(type==NOTIFICATION_FLAPPINGSTOP)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("FLAPPINGSTOP");
		else if(type==NOTIFICATION_FLAPPINGDISABLED)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("FLAPPINGDISABLED");
		else if(type==NOTIFICATION_DOWNTIMESTART)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("DOWNTIMESTART");
		else if(type==NOTIFICATION_DOWNTIMEEND)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("DOWNTIMEEND");
		else if(type==NOTIFICATION_DOWNTIMECANCELLED)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("DOWNTIMECANCELLED");
		else if(hst->current_state==HOST_UP)
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("RECOVERY");
		else
			macro_x[MACRO_NOTIFICATIONTYPE]=(char *)strdup("PROBLEM");

		/* set the notification number macro */
		my_free((void **)&macro_x[MACRO_HOSTNOTIFICATIONNUMBER]);
		asprintf(&macro_x[MACRO_HOSTNOTIFICATIONNUMBER],"%d",hst->current_notification_number);

		/* the $NOTIFICATIONNUMBER$ macro is maintained for backward compatability */
		my_free((void **)&macro_x[MACRO_NOTIFICATIONNUMBER]);
		macro_x[MACRO_NOTIFICATIONNUMBER]=(char *)strdup((macro_x[MACRO_HOSTNOTIFICATIONNUMBER]==NULL)?"":macro_x[MACRO_HOSTNOTIFICATIONNUMBER]);

		/* set the notification id macro */
		my_free((void **)&macro_x[MACRO_HOSTNOTIFICATIONID]);
		asprintf(&macro_x[MACRO_HOSTNOTIFICATIONID],"%lu",hst->current_notification_id);

		/* notify each contact (duplicates have been removed) */
		for(temp_notification=notification_list;temp_notification!=NULL;temp_notification=temp_notification->next){

			/* grab the macro variables for this contact */
			grab_contact_macros(temp_notification->contact);

			/* grab summary macros (customized for this contact) */
			grab_summary_macros(temp_notification->contact);

			/* notify this contact */
			result=notify_contact_of_host(temp_notification->contact,hst,type,ack_author,ack_data,escalated);

			/* keep track of how many contacts were notified */
			if(result==OK)
				contacts_notified++;
	                }

		/* free memory allocated to the notification list */
		free_notification_list();

		if(type==NOTIFICATION_NORMAL){

			/* adjust last/next notification time and notification flags if we notified someone */
			if(contacts_notified>0){

				/* calculate the next acceptable re-notification time */
				hst->next_host_notification=get_next_host_notification_time(hst,current_time);

				log_debug_info(DEBUGL_NOTIFICATIONS,0,"No contacts were notified.  Next possible notification time: %s",ctime(&hst->next_host_notification));

				/* update the last notification time for this host (this is needed for scheduling the next problem notification) */
				hst->last_host_notification=current_time;

				/* update notifications flags */
				if(hst->current_state==HOST_DOWN)
					hst->notified_on_down=TRUE;
				else if(hst->current_state==HOST_UNREACHABLE)
					hst->notified_on_unreachable=TRUE;
			        }

			/* we didn't end up notifying anyone, so adjust current notification number */
			else
				hst->current_notification_number--;
		        }

		log_debug_info(DEBUGL_NOTIFICATIONS,0,"%d contacts were notified.",contacts_notified);
	        }

	/* there were no contacts, so no notification really occurred... */
	else{

		/* adjust notification number, since no notification actually went out */
		if(type==NOTIFICATION_NORMAL)
			hst->current_notification_number--;

		log_debug_info(DEBUGL_NOTIFICATIONS,0,"No contacts were found for notification purposes.  No notification was sent out.\n");
	        }

	/* get the time we finished */
	gettimeofday(&end_time,NULL);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_notification_data(NEBTYPE_NOTIFICATION_END,NEBFLAG_NONE,NEBATTR_NONE,HOST_NOTIFICATION,type,start_time,end_time,(void *)hst,ack_author,ack_data,escalated,contacts_notified,NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst,FALSE);

	return OK;
        }



/* checks viability of sending a host notification */
int check_host_notification_viability(host *hst, int type){
	time_t current_time;
	time_t timeperiod_start;

	log_debug_info(DEBUGL_FUNCTIONS,0,"check_host_notification_viability()\n");

	/* get current time */
	time(&current_time);

	/* are notifications enabled? */
	if(enable_notifications==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Notifications are disabled, so host notifications (problems or acknowledgements) will not be sent out.\n");
		return ERROR;
	        }

	/* see if the host can have notifications sent out at this time */
	if(check_time_against_period(current_time,hst->notification_period_ptr)==ERROR){

		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This host shouldn't have notifications sent out at this time.\n");

		/* if this is a normal notification, calculate the next acceptable notification time, once the next valid time range arrives... */
		if(type==NOTIFICATION_NORMAL){

			get_next_valid_time(current_time,&timeperiod_start,hst->notification_period_ptr);

			/* it looks like there is no notification time defined, so schedule next one far into the future (one year)... */
			if(timeperiod_start==(time_t)0)
				hst->next_host_notification=(time_t)(current_time+(60*60*24*365));

			/* else use the next valid notification time */
			else
				hst->next_host_notification=timeperiod_start;

			log_debug_info(DEBUGL_NOTIFICATIONS,1,"Next possible notification time: %s\n",ctime(&hst->next_host_notification));
		        }

		return ERROR;
	        }

	/* are notifications temporarily disabled for this host? */
	if(hst->notifications_enabled==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Notifications are temporarily disabled for this host, so we won't send one out.\n");
		return ERROR;
	        }


	/****************************************/
	/*** SPECIAL CASE FOR ACKNOWLEGEMENTS ***/
	/****************************************/

	/* acknowledgements only have to pass three general filters, although they have another test of their own... */
	if(type==NOTIFICATION_ACKNOWLEDGEMENT){

		/* don't send an acknowledgement if there isn't a problem... */
		if(hst->current_state==HOST_UP){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"The host is currently UP, so we won't send an acknowledgement.\n");
			return ERROR;
	                }

		/* acknowledgement viability test passed, so the notification can be sent out */
		return OK;
	        }


	/*****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/*****************************************/

	/* flapping notifications only have to pass three general filters */
	if(type==NOTIFICATION_FLAPPINGSTART || type==NOTIFICATION_FLAPPINGSTOP || type==NOTIFICATION_FLAPPINGDISABLED){

		/* don't send a notification if we're not supposed to... */
		if(hst->notify_on_flapping==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about FLAPPING events for this host.\n");
			return ERROR;
	                }

		/* don't send notifications during scheduled downtime */
		if(hst->scheduled_downtime_depth>0){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about FLAPPING events during scheduled downtime.\n");
			return ERROR;
		        }

		/* flapping viability test passed, so the notification can be sent out */
		return OK;
	        }


	/*****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/*****************************************/

	/* flapping notifications only have to pass three general filters */
	if(type==NOTIFICATION_DOWNTIMESTART || type==NOTIFICATION_DOWNTIMEEND || type==NOTIFICATION_DOWNTIMECANCELLED){

		/* don't send a notification if we're not supposed to... */
		if(hst->notify_on_downtime==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about DOWNTIME events for this host.\n");
			return ERROR;
	                }

		/* don't send notifications during scheduled downtime */
		if(hst->scheduled_downtime_depth>0){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about DOWNTIME events during scheduled downtime!\n");
			return ERROR;
		        }

		/* downtime viability test passed, so the notification can be sent out */
		return OK;
	        }


	/****************************************/
	/*** NORMAL NOTIFICATIONS ***************/
	/****************************************/

	/* is this a hard problem/recovery? */
	if(hst->state_type==SOFT_STATE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This host is in a soft state, so we won't send a notification out.\n");
		return ERROR;
	        }

	/* has this problem already been acknowledged? */
	if(hst->problem_has_been_acknowledged==TRUE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This host problem has already been acknowledged, so we won't send a notification out!\n");
		return ERROR;
	        }

	/* check notification dependencies */
	if(check_host_dependencies(hst,NOTIFICATION_DEPENDENCY)==DEPENDENCIES_FAILED){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Notification dependencies for this host have failed, so we won't sent a notification out!\n");
		return ERROR;
	        }

	/* see if we should notify about problems with this host */
	if(hst->current_state==HOST_UNREACHABLE && hst->notify_on_unreachable==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about UNREACHABLE status for this host.\n");
		return ERROR;
	        }
	if(hst->current_state==HOST_DOWN && hst->notify_on_down==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about DOWN states for this host.\n");
		return ERROR;
	        }
	if(hst->current_state==HOST_UP){

		if(hst->notify_on_recovery==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about RECOVERY states for this host.\n");
			return ERROR;
		       }
		if(!(hst->notified_on_down==TRUE || hst->notified_on_unreachable==TRUE)){
			log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't notify about this recovery.\n");
			return ERROR;
		        }

	        }

	/* see if enough time has elapsed for first notification (Mathias Sundman) */
	if(current_time < (time_t)(hst->last_hard_state_change + (hst->first_notification_delay*interval_length))){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Not enough time has elapsed since the host since the host changed state, so we shouldn't notify about this yet.\n");
		return ERROR;
	        }

	/* if this host is currently flapping, don't send the notification */
	if(hst->is_flapping==TRUE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This host is currently flapping, so we won't send notifications.\n");
		return ERROR;
	        }

	/***** RECOVERY NOTIFICATIONS ARE GOOD TO GO AT THIS POINT *****/
	if(hst->current_state==HOST_UP)
		return OK;

	/* if this host is currently in a scheduled downtime period, don't send the notification */
	if(hst->scheduled_downtime_depth>0){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"This host is currently in a scheduled downtime, so we won't send notifications.\n");
		return ERROR;
	        }

	/* check if we shouldn't renotify contacts about the host problem */
	if(hst->no_more_notifications==TRUE){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"We shouldn't re-notify contacts about this host problem.\n");
		return ERROR;
	        }

	/* check if its time to re-notify the contacts about the host... */
	if(current_time < hst->next_host_notification){
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Its not yet time to re-notify the contacts about this host problem...\n");
		log_debug_info(DEBUGL_NOTIFICATIONS,1,"Next acceptable notification time: %s",ctime(&hst->next_host_notification));
		return ERROR;
	        }

	return OK;
        }



/* checks the viability of notifying a specific contact about a host */
int check_contact_host_notification_viability(contact *cntct, host *hst, int type){

	log_debug_info(DEBUGL_FUNCTIONS,0,"check_contact_host_notification_viability()\n");

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Checking host notification viability for contact '%s'...\n",cntct->name);

	/* are notifications enabled? */
	if(cntct->host_notifications_enabled==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"Host notifications are disabled for this contact.\n");
		return ERROR;
	        }

	/* see if the contact can be notified at this time */
	if(check_time_against_period(time(NULL),cntct->host_notification_period_ptr)==ERROR){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"This contact shouldn't be notified at this time.\n");
		return ERROR;
	        }


	/****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/****************************************/

	if(type==NOTIFICATION_FLAPPINGSTART || type==NOTIFICATION_FLAPPINGSTOP || type==NOTIFICATION_FLAPPINGDISABLED){

		if(cntct->notify_on_host_flapping==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about FLAPPING host events.\n");
			return ERROR;
		        }

		return OK;
	        }


	/****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/****************************************/

	if(type==NOTIFICATION_DOWNTIMESTART || type==NOTIFICATION_DOWNTIMEEND || type==NOTIFICATION_DOWNTIMECANCELLED){

		if(cntct->notify_on_host_downtime==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about DOWNTIME host events.\n");
			return ERROR;
		        }

		return OK;
	        }


	/*************************************/
	/*** ACKS AND NORMAL NOTIFICATIONS ***/
	/*************************************/

	/* see if we should notify about problems with this host */
	if(hst->current_state==HOST_DOWN && cntct->notify_on_host_down==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about DOWN states.\n");
		return ERROR;
	        }

	if(hst->current_state==HOST_UNREACHABLE && cntct->notify_on_host_unreachable==FALSE){
		log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about UNREACHABLE states,\n");
		return ERROR;
	        }

	if(hst->current_state==HOST_UP){

		if(cntct->notify_on_host_recovery==FALSE){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify this contact about RECOVERY states.\n");
			return ERROR;
		        }

		if(!((hst->notified_on_down==TRUE && cntct->notify_on_host_down==TRUE) || (hst->notified_on_unreachable==TRUE && cntct->notify_on_host_unreachable==TRUE))){
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"We shouldn't notify about this recovery.\n");
			return ERROR;
		        }

	        }

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Host notification viability for contact '%s' PASSED.\n",cntct->name);

	return OK;
        }



/* notify a specific contact that an entire host is down or up */
int notify_contact_of_host(contact *cntct, host *hst, int type, char *ack_author, char *ack_data, int escalated){
	commandsmember *temp_commandsmember;
	char command_name[MAX_INPUT_BUFFER];
	char *command_name_ptr;
	char *temp_buffer=NULL;
	char raw_command[MAX_COMMAND_BUFFER];
	char processed_command[MAX_COMMAND_BUFFER];
	int early_timeout=FALSE;
	double exectime;
	struct timeval start_time;
	struct timeval end_time;
	struct timeval method_start_time;
	struct timeval method_end_time;
	int macro_options=STRIP_ILLEGAL_MACRO_CHARS|ESCAPE_MACRO_CHARS;


	log_debug_info(DEBUGL_FUNCTIONS,0,"notify_contact_of_host()\n");
	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Attempting to notifying contact '%s'...\n",cntct->name);

	/* check viability of notifying this user about the host */
	/* acknowledgements are no longer excluded from this test - added 8/19/02 Tom Bertelson */
	if(check_contact_host_notification_viability(cntct,hst,type)==ERROR)
		return ERROR;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"** Notifying contact '%s'\n",cntct->name);

	/* get start time */
	gettimeofday(&start_time,NULL);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	end_time.tv_sec=0L;
	end_time.tv_usec=0L;
	broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_START,NEBFLAG_NONE,NEBATTR_NONE,HOST_NOTIFICATION,type,start_time,end_time,(void *)hst,cntct,ack_author,ack_data,escalated,NULL);
#endif

	/* process all the notification commands this user has */
	for(temp_commandsmember=cntct->host_notification_commands;temp_commandsmember!=NULL;temp_commandsmember=temp_commandsmember->next){

		/* get start time */
		gettimeofday(&method_start_time,NULL);

#ifdef USE_EVENT_BROKER
		/* send data to event broker */
		method_end_time.tv_sec=0L;
		method_end_time.tv_usec=0L;
		broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_START,NEBFLAG_NONE,NEBATTR_NONE,HOST_NOTIFICATION,type,method_start_time,method_end_time,(void *)hst,cntct,temp_commandsmember->command,ack_author,ack_data,escalated,NULL);
#endif

		/* get the command name */
		strncpy(command_name,temp_commandsmember->command,sizeof(command_name));
		command_name[sizeof(command_name)-1]='\x0';
		command_name_ptr=strtok(command_name,"!");

		/* get the raw command line */
		get_raw_command_line(temp_commandsmember->command_ptr,temp_commandsmember->command,raw_command,sizeof(raw_command),macro_options);

		/* process any macros contained in the argument */
		process_macros(raw_command,processed_command,sizeof(processed_command),macro_options);

		/* run the notification command */
		if(strcmp(processed_command,"")){

			log_debug_info(DEBUGL_NOTIFICATIONS,2,"Raw Command: %s\n",raw_command);
			log_debug_info(DEBUGL_NOTIFICATIONS,2,"Processed Command: %s\n",processed_command);

			/* log the notification to program log file */
			if(log_notifications==TRUE){
				switch(type){
				case NOTIFICATION_ACKNOWLEDGEMENT:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;ACKNOWLEDGEMENT (%s);%s;%s;%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT],macro_x[MACRO_HOSTACKAUTHOR],macro_x[MACRO_HOSTACKCOMMENT]);
					break;
				case NOTIFICATION_FLAPPINGSTART:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;FLAPPINGSTART (%s);%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT]);
					break;
				case NOTIFICATION_FLAPPINGSTOP:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;FLAPPINGSTOP (%s);%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT]);
					break;
				case NOTIFICATION_FLAPPINGDISABLED:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;FLAPPINGDISABLED (%s);%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT]);
					break;
				case NOTIFICATION_DOWNTIMESTART:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;DOWNTIMESTART (%s);%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT]);
					break;
				case NOTIFICATION_DOWNTIMEEND:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;DOWNTIMEEND (%s);%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT]);
					break;
				case NOTIFICATION_DOWNTIMECANCELLED:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;DOWNTIMECANCELLED (%s);%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT]);
					break;
				default:
					asprintf(&temp_buffer,"HOST NOTIFICATION: %s;%s;%s;%s;%s\n",cntct->name,hst->name,macro_x[MACRO_HOSTSTATE],command_name_ptr,macro_x[MACRO_HOSTOUTPUT]);
					break;
				        }
				write_to_all_logs(temp_buffer,NSLOG_HOST_NOTIFICATION);
				my_free((void **)&temp_buffer);
			        }

			/* run the command */
			my_system(processed_command,notification_timeout,&early_timeout,&exectime,NULL,0);

			/* check to see if the notification timed out */
			if(early_timeout==TRUE){
				asprintf(&temp_buffer,"Warning: Contact '%s' host notification command '%s' timed out after %d seconds\n",cntct->name,processed_command,DEFAULT_NOTIFICATION_TIMEOUT);
				write_to_logs_and_console(temp_buffer,NSLOG_HOST_NOTIFICATION | NSLOG_RUNTIME_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
		        }

		/* get end time */
		gettimeofday(&method_end_time,NULL);

#ifdef USE_EVENT_BROKER
		/* send data to event broker */
		broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_END,NEBFLAG_NONE,NEBATTR_NONE,HOST_NOTIFICATION,type,method_start_time,method_end_time,(void *)hst,cntct,temp_commandsmember->command,ack_author,ack_data,escalated,NULL);
#endif
	        }

	/* get end time */
	gettimeofday(&end_time,NULL);

	/* update the contact's last host notification time */
	cntct->last_host_notification=start_time.tv_sec;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_END,NEBFLAG_NONE,NEBATTR_NONE,HOST_NOTIFICATION,type,start_time,end_time,(void *)hst,cntct,ack_author,ack_data,escalated,NULL);
#endif

	return OK;
        }


/* checks to see if a host escalation entry is a match for the current host notification */
int is_valid_host_escalation_for_host_notification(host *hst, hostescalation *he){
	int notification_number=0;
	time_t current_time=0L;
	host *temp_host=NULL;

	log_debug_info(DEBUGL_FUNCTIONS,0,"is_valid_escalation_for_host_notification()\n");

	/* get the current time */
	time(&current_time);

	/* if this is a recovery, really we check for who got notified about a previous problem */
	if(hst->current_state==HOST_UP)
		notification_number=hst->current_notification_number-1;
	else
		notification_number=hst->current_notification_number;

	/* find the host this escalation entry is associated with */
	temp_host=he->host_ptr;
	if(temp_host==NULL || temp_host!=hst)
		return FALSE;

	/* skip this escalation if it happens later */
	if(he->first_notification > notification_number)
		return FALSE;

	/* skip this escalation if it has already passed */
	if(he->last_notification!=0 && he->last_notification < notification_number)
		return FALSE;

	/* skip this escalation if it has a timeperiod and the current time isn't valid */
	if(he->escalation_period!=NULL && check_time_against_period(current_time,he->escalation_period_ptr)==ERROR)
		return FALSE;

	/* skip this escalation if the state options don't match */
	if(hst->current_state==HOST_UP && he->escalate_on_recovery==FALSE)
		return FALSE;
	else if(hst->current_state==HOST_DOWN && he->escalate_on_down==FALSE)
		return FALSE;
	else if(hst->current_state==HOST_UNREACHABLE && he->escalate_on_unreachable==FALSE)
		return FALSE;

	return TRUE;
        }



/* checks to see whether a host notification should be escalation */
int should_host_notification_be_escalated(host *hst){
	hostescalation *temp_he;

	log_debug_info(DEBUGL_FUNCTIONS,0,"should_host_notification_be_escalated()\n");

	/* search the host escalation list */
	for(temp_he=hostescalation_list;temp_he!=NULL;temp_he=temp_he->next){

		/* we found a matching entry, so escalate this notification! */
		if(is_valid_host_escalation_for_host_notification(hst,temp_he)==TRUE)
			return TRUE;
	        }

	log_debug_info(DEBUGL_NOTIFICATIONS,1,"Host notification will NOT be escalated.\n");

	return FALSE;
        }


/* given a host, create a list of contacts to be notified, removing duplicates */
int create_notification_list_from_host(host *hst, int *escalated){
	hostescalation *temp_he=NULL;
	contactsmember *temp_contactsmember=NULL;
	contact *temp_contact=NULL;

	log_debug_info(DEBUGL_FUNCTIONS,0,"create_notification_list_from_host()\n");

	/* see if this notification should be escalated */
	if(should_host_notification_be_escalated(hst)==TRUE){

		/* set the escalation flag */
		*escalated=TRUE;

		/* check all the host escalation entries */
		for(temp_he=hostescalation_list;temp_he!=NULL;temp_he=temp_he->next){

			/* see if this escalation if valid for this notification */
			if(is_valid_host_escalation_for_host_notification(hst,temp_he)==FALSE)
				continue;

			log_debug_info(DEBUGL_NOTIFICATIONS,1,"Adding contacts from host escalation to notification list.\n");

			/* find each contact in this escalation entry */
			for(temp_contactsmember=temp_he->contacts;temp_contactsmember!=NULL;temp_contactsmember=temp_contactsmember->next){
				if((temp_contact=temp_contactsmember->contact_ptr)==NULL)
					continue;
				add_notification(temp_contact);
			        }
		        }
	        }

	/* else we shouldn't escalate the notification, so continue as normal... */
	else{

		/* set the escalation flag */
		*escalated=FALSE;

		/* get all contacts for this host */
		for(temp_contactsmember=hst->contacts;temp_contactsmember!=NULL;temp_contactsmember=temp_contactsmember->next){
			if((temp_contact=temp_contactsmember->contact_ptr)==NULL)
				continue;
			add_notification(temp_contact);
			}
	        }

	return OK;
        }




/******************************************************************/
/***************** NOTIFICATION TIMING FUNCTIONS ******************/
/******************************************************************/


/* calculates next acceptable re-notification time for a service */
time_t get_next_service_notification_time(service *svc, time_t offset){
	time_t next_notification=0L;
	double interval_to_use=0.0;
	serviceescalation *temp_se=NULL;
	int have_escalated_interval=FALSE;

	log_debug_info(DEBUGL_FUNCTIONS,0,"get_next_service_notification_time()\n");

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Calculating next valid notification time...\n");

	/* default notification interval */
	interval_to_use=svc->notification_interval;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Default interval: %d\n",interval_to_use);

	/* search all the escalation entries for valid matches for this service (at its current notification number) */
	for(temp_se=serviceescalation_list;temp_se!=NULL;temp_se=temp_se->next){

		/* interval < 0 means to use non-escalated interval */
		if(temp_se->notification_interval<0.0)
			continue;

		/* skip this entry if it isn't appropriate */
		if(is_valid_escalation_for_service_notification(svc,temp_se)==FALSE)
			continue;

		log_debug_info(DEBUGL_NOTIFICATIONS,2,"Found a valid escalation w/ interval of %f\n",temp_se->notification_interval);

		/* if we haven't used a notification interval from an escalation yet, use this one */
		if(have_escalated_interval==FALSE){
			have_escalated_interval=TRUE;
			interval_to_use=temp_se->notification_interval;
		        }

		/* else use the shortest of all valid escalation intervals */
		else if(temp_se->notification_interval<interval_to_use)
			interval_to_use=temp_se->notification_interval;

		log_debug_info(DEBUGL_NOTIFICATIONS,2,"New interval: %f\n",interval_to_use);
	        }

	/* if notification interval is 0, we shouldn't send any more problem notifications (unless service is volatile) */
	if(interval_to_use==0.0 && svc->is_volatile==FALSE)
		svc->no_more_notifications=TRUE;
	else
		svc->no_more_notifications=FALSE;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Interval used for calculating next valid notification time: %f\n",interval_to_use);

	/* calculate next notification time */
	next_notification=offset+(interval_to_use*interval_length);

	return next_notification;
        }



/* calculates next acceptable re-notification time for a host */
time_t get_next_host_notification_time(host *hst, time_t offset){
	time_t next_notification=0L;
	double interval_to_use=0.0;
	hostescalation *temp_he=NULL;
	int have_escalated_interval=FALSE;


	log_debug_info(DEBUGL_FUNCTIONS,0,"get_next_host_notification_time()\n");

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Calculating next valid notification time...\n");

	/* default notification interval */
	interval_to_use=hst->notification_interval;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Default interval: %d\n",interval_to_use);

	/* check all the host escalation entries for valid matches for this host (at its current notification number) */
	for(temp_he=hostescalation_list;temp_he!=NULL;temp_he=temp_he->next){

		/* interval < 0 means to use non-escalated interval */
		if(temp_he->notification_interval<0.0)
			continue;

		/* skip this entry if it isn't appropriate */
		if(is_valid_host_escalation_for_host_notification(hst,temp_he)==FALSE)
			continue;

		log_debug_info(DEBUGL_NOTIFICATIONS,2,"Found a valid escalation w/ interval of %f\n",temp_he->notification_interval);

		/* if we haven't used a notification interval from an escalation yet, use this one */
		if(have_escalated_interval==FALSE){
			have_escalated_interval=TRUE;
			interval_to_use=temp_he->notification_interval;
		        }

		/* else use the shortest of all valid escalation intervals  */
		else if(temp_he->notification_interval<interval_to_use)
			interval_to_use=temp_he->notification_interval;

		log_debug_info(DEBUGL_NOTIFICATIONS,2,"New interval: %f\n",interval_to_use);
	        }

	/* if interval is 0, no more notifications should be sent */
	if(interval_to_use==0.0)
		hst->no_more_notifications=TRUE;
	else
		hst->no_more_notifications=FALSE;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Interval used for calculating next valid notification time: %f\n",interval_to_use);

	/* calculate next notification time */
	next_notification=offset+(interval_to_use*interval_length);

	return next_notification;
        }



/******************************************************************/
/***************** NOTIFICATION OBJECT FUNCTIONS ******************/
/******************************************************************/


/* given a contact name, find the notification entry for them for the list in memory */
notification * find_notification(contact *cntct){
	notification *temp_notification=NULL;

	log_debug_info(DEBUGL_FUNCTIONS,0,"find_notification() start\n");

	if(cntct==NULL)
		return NULL;
	
	for(temp_notification=notification_list;temp_notification!=NULL;temp_notification=temp_notification->next){
		if(temp_notification->contact==cntct)
			return temp_notification;
	        }

	/* we couldn't find the contact in the notification list */
	return NULL;
        }
	


/* add a new notification to the list in memory */
int add_notification(contact *cntct){
	notification *new_notification=NULL;
	notification *temp_notification=NULL;

	log_debug_info(DEBUGL_FUNCTIONS,0,"add_notification() start\n");

	if(cntct==NULL)
		return ERROR;

	log_debug_info(DEBUGL_NOTIFICATIONS,2,"Adding contact '%s' to notification list.\n",cntct->name);

	/* don't add anything if this contact is already on the notification list */
	if((temp_notification=find_notification(cntct))!=NULL)
		return OK;

	/* allocate memory for a new contact in the notification list */
	if((new_notification=malloc(sizeof(notification)))==NULL)
		return ERROR;

	/* fill in the contact info */
	new_notification->contact=cntct;

	/* add new notification to head of list */
	new_notification->next=notification_list;
	notification_list=new_notification;

	return OK;
        }

