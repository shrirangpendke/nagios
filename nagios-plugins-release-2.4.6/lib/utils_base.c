/*****************************************************************************
*
* utils_base.c
*
* License: GPL
* Copyright (c) 2006-2014 Nagios Plugins Development Team
*
* Library of useful functions for plugins
* 
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* 
*
*****************************************************************************/

#include "common.h"
#include <stdarg.h>
#include "utils_base.h"
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>

#define np_free(ptr) { if(ptr) { free(ptr); ptr = NULL; } }

nagios_plugin *this_nagios_plugin=NULL;

int _np_state_read_file(FILE *);

void np_init( char *plugin_name, int argc, char **argv ) {
	if (!this_nagios_plugin) {
		this_nagios_plugin = calloc(1, sizeof(nagios_plugin));
		if (!this_nagios_plugin) {
			die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));
		}
		this_nagios_plugin->plugin_name = strdup(plugin_name);
		if (!this_nagios_plugin->plugin_name)
			die(STATE_UNKNOWN, "%s %s\n", _("Cannot execute strdup:"), strerror(errno));
		this_nagios_plugin->argc = argc;
		this_nagios_plugin->argv = argv;
	}
}

void np_set_args( int argc, char **argv ) {
	if (!this_nagios_plugin)
		die(STATE_UNKNOWN, "%s\n", _("This requires np_init to be called"));

	this_nagios_plugin->argc = argc;
	this_nagios_plugin->argv = argv;
}


void np_cleanup() {
	if (this_nagios_plugin) {
		if(this_nagios_plugin->state) {
			if(this_nagios_plugin->state->state_data) { 
				np_free(this_nagios_plugin->state->state_data->data);
				np_free(this_nagios_plugin->state->state_data);
			}
			np_free(this_nagios_plugin->state->name);
			np_free(this_nagios_plugin->state);
		}
		np_free(this_nagios_plugin->plugin_name);
		np_free(this_nagios_plugin);
	}
	this_nagios_plugin=NULL;
}

/* Hidden function to get a pointer to this_nagios_plugin for testing */
void _get_nagios_plugin( nagios_plugin **pointer ){
	*pointer = this_nagios_plugin;
}

void
die (int result, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vprintf (fmt, ap);
	va_end (ap);
	if(this_nagios_plugin) {
		np_cleanup();
	}
	exit (result);
}

void set_range_start (range *this, double value) {
	this->start = value;
	this->start_infinity = FALSE;
}

void set_range_end (range *this, double value) {
	this->end = value;
	this->end_infinity = FALSE;
}

range
*parse_range_string (char *str) {
	range *temp_range;
	double start;
	double end;
	char *end_str;

	temp_range = (range *) calloc(1, sizeof(range));

	/* Set defaults */
	temp_range->start = 0;
	temp_range->start_infinity = FALSE;
	temp_range->end = 0;
	temp_range->end_infinity = TRUE;
	temp_range->alert_on = OUTSIDE;

	if (str[0] == '@') {
		temp_range->alert_on = INSIDE;
		str++;
	}

	end_str = index(str, ':');
	if (end_str) {
		if (str[0] == '~') {
			temp_range->start_infinity = TRUE;
		} else {
			start = strtod(str, NULL);	/* Will stop at the ':' */
			set_range_start(temp_range, start);
		}
		end_str++;		/* Move past the ':' */
	} else {
		end_str = str;
	}
	end = strtod(end_str, NULL);
	if (strcmp(end_str, "")) {
		set_range_end(temp_range, end);
	}

	if (temp_range->start_infinity ||
		temp_range->end_infinity ||
		temp_range->start <= temp_range->end) {
		return temp_range;
	}
	free(temp_range);
	return NULL;
}

/* returns 0 if okay, otherwise 1 */
int
_set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string)
{
	thresholds *temp_thresholds = NULL;

	if (!(temp_thresholds = calloc(1, sizeof(thresholds))))
		die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));

	temp_thresholds->warning = NULL;
	temp_thresholds->critical = NULL;
	temp_thresholds->warning_string = NULL;
	temp_thresholds->critical_string = NULL;

	if (warn_string) {
		if (!(temp_thresholds->warning = parse_range_string(warn_string))) {
			free(temp_thresholds);
			return NP_RANGE_UNPARSEABLE;
		}
		temp_thresholds->warning_string = strdup(warn_string);
	}
	if (critical_string) {
		if (!(temp_thresholds->critical = parse_range_string(critical_string))) {
			free(temp_thresholds);
			return NP_RANGE_UNPARSEABLE;
		}
		temp_thresholds->critical_string = strdup(critical_string);
	}

	*my_thresholds = temp_thresholds;

	return 0;
}

void
set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string)
{
	switch (_set_thresholds(my_thresholds, warn_string, critical_string)) {
	case 0:
		return;
	case NP_RANGE_UNPARSEABLE:
		die(STATE_UNKNOWN, "%s\n", _("Range format incorrect"));
	case NP_WARN_WITHIN_CRIT:
		die(STATE_UNKNOWN, "%s\n", _("Warning level is a subset of critical and will not be alerted"));
		break;
	}
}

void print_thresholds(const char *threshold_name, thresholds *my_threshold) {
	printf("%s - ", threshold_name);
	if (! my_threshold) {
		printf("Threshold not set");
	} else {
		if (my_threshold->warning) {
			printf("Warning: start=%g end=%g; ", my_threshold->warning->start, my_threshold->warning->end);
			if (my_threshold->warning_string) {
				printf("Warning String: %s; ", my_threshold->warning_string);
			}
		} else {
			printf("Warning not set; ");
		}
		if (my_threshold->critical) {
			printf("Critical: start=%g end=%g", my_threshold->critical->start, my_threshold->critical->end);
			if (my_threshold->critical_string) {
				printf("Critical String: %s; ", my_threshold->critical_string);
			}
		} else {
			printf("Critical not set");
		}
	}
	printf("\n");
}

/* Returns TRUE if alert should be raised based on the range */
int
check_range(double value, range *my_range)
{
	int no = FALSE;
	int yes = TRUE;

	if (my_range->alert_on == INSIDE) {
		no = TRUE;
		yes = FALSE;
	}

	if (!my_range->end_infinity && !my_range->start_infinity) {
		if ((my_range->start <= value) && (value <= my_range->end)) {
			return no;
		} else {
			return yes;
		}
	} else if (!my_range->start_infinity && my_range->end_infinity) {
		if (my_range->start <= value) {
			return no;
		} else {
			return yes;
		}
	} else if (my_range->start_infinity && !my_range->end_infinity) {
		if (value <= my_range->end) {
			return no;
		} else {
			return yes;
		}
	} else {
		return no;
	}
}

/* Returns status */
int
get_status(double value, thresholds *my_thresholds)
{
	if (my_thresholds->critical) {
		if (check_range(value, my_thresholds->critical)) {
			return STATE_CRITICAL;
		}
	}
	if (my_thresholds->warning) {
		if (check_range(value, my_thresholds->warning)) {
			return STATE_WARNING;
		}
	}
	return STATE_OK;
}

char *np_escaped_string (const char *string) {
	char *data;
	int i, j=0;
	data = strdup(string);
	for (i=0; data[i]; i++) {
		if (data[i] == '\\') {
			switch(data[++i]) {
				case 'n':
					data[j++] = '\n';
					break;
				case 'r':
					data[j++] = '\r';
					break;
				case 't':
					data[j++] = '\t';
					break;
				case '\\':
					data[j++] = '\\';
					break;
				default:
					data[j++] = data[i];
			}
		} else {
			data[j++] = data[i];
		}
	}
	data[j] = '\0';
	return data;
}

int np_check_if_root(void) { return (geteuid() == 0); }

int np_warn_if_not_root(void) {
	int status = np_check_if_root();
	if(!status) {
		printf("%s", _("Warning: "));
		printf("%s\n", _("This plugin must be either run as root or setuid root."));
		printf("%s\n", _("To run as root, you can use a tool like sudo."));
		printf("%s\n", _("To set the setuid permissions, use the command:"));
		/* XXX could we use something like progname? */
		printf("\t%s\n", _("chmod u+s yourpluginfile"));
	}
	return status;
}

/*
 * Extract the value from key/value pairs, or return NULL. The value returned
 * can be free()ed.
 * This function can be used to parse NTP control packet data and performance
 * data strings.
 */
char *np_extract_value(const char *varlist, const char *name, char sep) {
	char *tmp=NULL, *value=NULL;
	int i;
	size_t varlistlen;

	while (1) {
		/* Strip any leading space */
		for (; isspace(varlist[0]); varlist++);

		if (!strncmp(name, varlist, strlen(name))) {
			varlist += strlen(name);
			/* strip trailing spaces */
			for (; isspace(varlist[0]); varlist++);

			if (varlist[0] == '=') {
				/* We matched the key, go past the = sign */
				varlist++;
				/* strip leading spaces */
				for (; isspace(varlist[0]); varlist++);

				if ((tmp = index(varlist, sep))) {
					/* Value is delimited by a comma */
					if (tmp-varlist == 0) continue;
					value = (char *)calloc(1, tmp-varlist+1);
					strncpy(value, varlist, tmp-varlist);
					value[tmp-varlist] = '\0';
				} else {
					/* Value is delimited by a \0 */
					varlistlen = strlen(varlist);
					if (!varlistlen) continue;
					value = (char *)calloc(1, varlistlen + 1);
					strncpy(value, varlist, varlistlen);
					value[varlistlen] = '\0';
				}
				break;
			}
		}
		if ((tmp = index(varlist, sep))) {
			/* More keys, keep going... */
			varlist = tmp + 1;
		} else {
			/* We're done */
			break;
		}
	}

	/* Clean-up trailing spaces/newlines */
	if (value) for (i=strlen(value)-1; isspace(value[i]); i--) value[i] = '\0';

	return value;
}

/*
 * Read a string representing a state (ok, warning... or numeric: 0, 1) and
 * return the corresponding STATE_ value or ERROR)
 */
int translate_state (char *state_text) {
       if (!strcasecmp(state_text,"OK") || !strcmp(state_text,"0"))
               return STATE_OK;
       if (!strcasecmp(state_text,"WARNING") || !strcmp(state_text,"1"))
               return STATE_WARNING;
       if (!strcasecmp(state_text,"CRITICAL") || !strcmp(state_text,"2"))
               return STATE_CRITICAL;
       if (!strcasecmp(state_text,"UNKNOWN") || !strcmp(state_text,"3"))
               return STATE_UNKNOWN;
       return ERROR;
}

/*
 * Returns a string to use as a keyname, based on an md5 hash of argv, thus
 * hopefully a unique key per service/plugin invocation. Use the extra-opts
 * parse of argv, so that uniqueness in parameters are reflected there.
 */
char *_np_state_generate_key() {
	struct sha1_ctx ctx;
	int i;
	char **argv = this_nagios_plugin->argv;
	unsigned char result[20];
	char keyname[41];
	char *p=NULL;

	sha1_init_ctx(&ctx);
	
	for(i=0; i<this_nagios_plugin->argc; i++) {
		sha1_process_bytes(argv[i], strlen(argv[i]), &ctx);
	}

	sha1_finish_ctx(&ctx, &result);
	
	for (i=0; i<20; ++i) {
		sprintf(&keyname[2*i], "%02x", result[i]);
	}
	keyname[40]='\0';
	
	p = strdup(keyname);
	if(!p) {
		die(STATE_UNKNOWN, "%s %s\n", _("Cannot execute strdup:"), strerror(errno));
	}
	return p;
}

void _cleanup_state_data() {
	if (this_nagios_plugin->state->state_data) {
		np_free(this_nagios_plugin->state->state_data->data);
		np_free(this_nagios_plugin->state->state_data);
	}
}

/*
 * Internal function. Returns either:
 *   envvar NAGIOS_PLUGIN_STATE_DIRECTORY
 *   statically compiled shared state directory
 */
char* _np_state_calculate_location_prefix(){
	char *env_dir;

	/* Do not allow passing NP_STATE_DIRECTORY in setuid plugins
	 * for security reasons */

	if (!np_suid()) {
		env_dir = getenv("NAGIOS_PLUGIN_STATE_DIRECTORY");
		if(env_dir && env_dir[0] != '\0')
			return env_dir;
	}
	return NP_STATE_DIR_PREFIX;
}

/*
 * Initiatializer for state routines.
 * Sets variables. Generates filename. Returns np_state_key. die with
 * UNKNOWN if exception
 */
void np_enable_state(char *keyname, int expected_data_version) {
	state_key *this_state = NULL;
	char *temp_filename = NULL;
	char *temp_keyname = NULL;
	char *p=NULL;
	int ret;

	if(!this_nagios_plugin)
		die(STATE_UNKNOWN, "%s\n", _("This requires np_init to be called"));

	this_state = (state_key *) calloc(1, sizeof(state_key));
	if(!this_state)
		die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));

	if(!keyname) {
		temp_keyname = _np_state_generate_key();
	} else {
		temp_keyname = strdup(keyname);
		if(!temp_keyname)
			die(STATE_UNKNOWN, "%s %s\n", _("Cannot execute strdup:"), strerror(errno));
	}
	/* Die if invalid characters used for keyname */
	p = temp_keyname;
	while(*p!='\0') {
		if(! (isalnum(*p) || *p == '_'))
			die(STATE_UNKNOWN, "%s\n", _("Invalid character for keyname - only alphanumerics or '_'"));
		p++;
	}
	this_state->name=temp_keyname;
	this_state->plugin_name=this_nagios_plugin->plugin_name;
	this_state->data_version=expected_data_version;
	this_state->state_data=NULL;

	/* Calculate filename */
	ret = asprintf(&temp_filename, "%s/%lu/%s/%s", _np_state_calculate_location_prefix(), (unsigned long)geteuid(), this_nagios_plugin->plugin_name, this_state->name);
	if (ret < 0)
		die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));
	this_state->_filename=temp_filename;

	this_nagios_plugin->state = this_state;
}

/*
 * Will return NULL if no data is available (first run). If key currently
 * exists, read data. If state file format version is not expected, return
 * as if no data. Get state data version number and compares to expected.
 * If numerically lower, then return as no previous state. die with UNKNOWN
 * if exceptional error.
 */
state_data *np_state_read() {
	state_data *this_state_data=NULL;
	FILE *statefile;
	int rc = FALSE;

	if(!this_nagios_plugin)
		die(STATE_UNKNOWN, "%s\n", _("This requires np_init to be called"));

	/* Open file. If this fails, no previous state found */
	statefile = fopen( this_nagios_plugin->state->_filename, "r" );
	if(statefile) {

		this_state_data = (state_data *) calloc(1, sizeof(state_data));
		if(!this_state_data)
			die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));

		this_state_data->data=NULL;
		this_nagios_plugin->state->state_data = this_state_data;

		rc = _np_state_read_file(statefile);

		fclose(statefile);
	}

	if(!rc) {
		_cleanup_state_data();
	}

	return this_nagios_plugin->state->state_data;
}

/* 
 * Read the state file
 */
int _np_state_read_file(FILE *f) {
	int status=FALSE;
	size_t pos;
	char *line;
	int i;
	int failure=0;
	time_t current_time, data_time;
	enum { STATE_FILE_VERSION, STATE_DATA_VERSION, STATE_DATA_TIME, STATE_DATA_TEXT, STATE_DATA_END } expected=STATE_FILE_VERSION;

	time(&current_time);

	/* Note: This introduces a limit of 1024 bytes in the string data */
	line = (char *) calloc(1, 1024);
	if(!line)
		die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));

	while(!failure && (fgets(line,1024,f))!=NULL){
		pos=strlen(line);
		if(line[pos-1]=='\n')
			line[pos-1]='\0';

		if(line[0] == '#') continue;

		switch(expected) {
			case STATE_FILE_VERSION:
				i=atoi(line);
				if(i!=NP_STATE_FORMAT_VERSION)
					failure++;
				else
					expected=STATE_DATA_VERSION;
				break;
			case STATE_DATA_VERSION:
				i=atoi(line);
				if(i != this_nagios_plugin->state->data_version)
					failure++;
				else
					expected=STATE_DATA_TIME;
				break;
			case STATE_DATA_TIME:
				/* If time > now, error */
				data_time=strtoul(line,NULL,10);
				if(data_time > current_time)
					failure++;
				else {
					this_nagios_plugin->state->state_data->time = data_time;
					expected=STATE_DATA_TEXT;
				}
				break;
			case STATE_DATA_TEXT:
				this_nagios_plugin->state->state_data->data = strdup(line);
				if(this_nagios_plugin->state->state_data->data==NULL)
					die(STATE_UNKNOWN, "%s %s\n", _("Cannot execute strdup:"), strerror(errno));
				expected=STATE_DATA_END;
				status=TRUE;
				break;
			case STATE_DATA_END:
				;
		}
	}

	np_free(line);
	return status;
}

/*
 * If time=NULL, use current time. Create state file, with state format 
 * version, default text. Writes version, time, and data. Avoid locking 
 * problems - use mv to write and then swap. Possible loss of state data if 
 * two things writing to same key at same time. 
 * Will die with UNKNOWN if errors
 */
void np_state_write_string(time_t data_time, char *data_string) {
	FILE *fp;
	char *temp_file=NULL;
	int fd=0, result=0;
	time_t current_time;
	char *directories=NULL;
	char *p=NULL;

	if(!data_time)
		time(&current_time);
	else
		current_time=data_time;
	
	/* If file doesn't currently exist, create directories */
	if(access(this_nagios_plugin->state->_filename,F_OK)) {
		result = asprintf(&directories, "%s", this_nagios_plugin->state->_filename);
		if (result < 0)
			die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));
		if(!directories)
			die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));

		for(p=directories+1; *p; p++) {
			if(*p=='/') {
				*p='\0';
				if(access(directories,F_OK) && mkdir(directories, S_IRWXU)) {
					/* Can't free this! Otherwise error message is wrong! */
					/* np_free(directories); */ 
					die(STATE_UNKNOWN, "%s %s\n", _("Cannot create directory:"), directories);
				}
				*p='/';
			}
		}
		np_free(directories);
	}

	result = asprintf(&temp_file,"%s.XXXXXX",this_nagios_plugin->state->_filename);
	if (result < 0)
		die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));
	if(!temp_file)
		die(STATE_UNKNOWN, "%s %s\n", _("Cannot allocate memory:"), strerror(errno));

	if((fd=mkstemp(temp_file))==-1) {
		np_free(temp_file);
		die(STATE_UNKNOWN, "%s\n", _("Cannot create temporary filename"));
	}

	fp=(FILE *)fdopen(fd,"w");
	if(!fp) {
		close(fd);
		unlink(temp_file);
		np_free(temp_file);
		die(STATE_UNKNOWN, "%s\n", _("Unable to open temporary state file"));
	}
	
	fprintf(fp,"# NP State file\n");
	fprintf(fp,"%d\n",NP_STATE_FORMAT_VERSION);
	fprintf(fp,"%d\n",this_nagios_plugin->state->data_version);
	fprintf(fp,"%lu\n",current_time);
	fprintf(fp,"%s\n",data_string);
	
	fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);
	
	fflush(fp);

	result=fclose(fp);

	fsync(fd);

	if(result) {
		unlink(temp_file);
		np_free(temp_file);
		die(STATE_UNKNOWN, "%s\n", _("Error writing temp file"));
	}

	if(rename(temp_file, this_nagios_plugin->state->_filename)) {
		unlink(temp_file);
		np_free(temp_file);
		die(STATE_UNKNOWN, "%s\n", _("Cannot rename state temp file"));
	}

	np_free(temp_file);
}
