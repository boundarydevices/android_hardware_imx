/*
* Copyright (C) 2011 The Android Open Source Project
* Copyright (C) 2011 Atheros Communications, Inc.
* Copyright (C) 2011-2014 Freescale Semiconductor, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <math.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <sys/inotify.h>
#include <poll.h>

#define  LOG_TAG  "athr_gps"

#include <cutils/log.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <hardware/gps.h>

/* for Atheros GPS devices*/
#define ATHR_GPS
#define ATHR_GPSXtra
#define ATHR_GPSNi

//#define  GPS_DEBUG
#undef	 GPS_DEBUG_TOKEN	/* print out NMEA tokens */

#define  DFR(...)   ALOGD(__VA_ARGS__)

#ifdef GPS_DEBUG
#  define  D(...)   ALOGD(__VA_ARGS__)
#else
#  define  D(...)   ((void)0)
#endif

#define GPS_STATUS_CB(_cb, _s)    \
  if ((_cb).status_cb) {          \
    GpsStatus gps_status;         \
    gps_status.status = (_s);     \
    (_cb).status_cb(&gps_status); \
    DFR("gps status callback: 0x%x", _s); \
  }

/* Nmea Parser stuff */
#define  NMEA_MAX_SIZE  83

enum {
  STATE_QUIT  = 0,
  STATE_INIT  = 1,
  STATE_START = 2
};

typedef struct
{
    int valid;
    double systime;
    GpsUtcTime timestamp;
} AthTimemap_t;

typedef struct {
    int     pos;
    int     overflow;
    int     utc_year;
    int     utc_mon;
    int     utc_day;
    int     utc_diff;
    GpsLocation  fix;
    GpsSvStatus  sv_status;
    int     sv_status_changed;
    char    in[ NMEA_MAX_SIZE+1 ];
    int     gsa_fixed;
    AthTimemap_t timemap;
} NmeaReader;

typedef struct {
    int                     init;
    int                     fd;
    GpsCallbacks            callbacks;
    pthread_t               thread;
    pthread_t		    nmea_thread;
    pthread_t               tmr_thread;
    int                     control[2];
    int                     fix_freq;
    sem_t                   fix_sem;
    int                     first_fix;
    NmeaReader              reader;
#ifdef ATHR_GPSXtra
    int			            xtra_init;
    GpsXtraCallbacks	    xtra_callbacks;
#endif
#ifdef ATHR_GPSNi
    int                     ni_init;
    GpsNiCallbacks          ni_callbacks;
    GpsNiNotification       ni_notification;
#endif
} GpsState;


GpsCallbacks* g_gpscallback = 0;

void gps_state_lock_fix(GpsState *state) {
    int ret;
    do {
        ret=sem_wait(&state->fix_sem);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        D("Error in GPS state lock:%s\n", strerror(errno));
    }
}

void gps_state_unlock_fix(GpsState *state) {
    if (sem_post(&state->fix_sem) == -1)
	{
		if(errno == EAGAIN)
			if(sem_post(&state->fix_sem)== -1)
				D("Error in GPS state unlock:%s\n", strerror(errno));
	}
}

int 	gps_opentty(GpsState *state);
void 	gps_closetty(GpsState *state);
void 	gps_wakeup(GpsState *state);
void 	gps_sleep(GpsState *state);
int 	gps_checkstate(GpsState *state);

static GpsState  _gps_state[1];
static GpsState *gps_state = _gps_state;
static int sleep_lock = 0;
static int lastcmd = 0;
static int bGetFormalNMEA = 1;
static int started    = 0;
static int continue_thread = 1;
static int bOrionShutdown = 0;

#define GPS_DEV_SLOW_UPDATE_RATE (10)
#define GPS_DEV_HIGH_UPDATE_RATE (1)

#define GPS_DEV_LOW_BAUD  (B9600)
#define GPS_DEV_HIGH_BAUD (B115200)

static void gps_nmea_thread( void*  arg );
static void gps_timer_thread( void*  arg );

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****                 U T I L I T I E S                     *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

#ifdef ATHR_GPSNi
static void ath_send_ni_notification(NmeaReader* r)
{
    //D("ath_send_ni_notification is called");
    if (gps_state->ni_init && gps_state->ni_callbacks.notify_cb)
    {
        memset(&gps_state->ni_notification, 0, sizeof(sizeof(gps_state->ni_notification)));
       gps_state->ni_notification.size                     = sizeof(GpsNiNotification);
        gps_state->ni_notification.notification_id          = 0xABBA;
        gps_state->ni_notification.ni_type                  = GPS_NI_TYPE_UMTS_CTRL_PLANE; // AM TBD: is it correct?
        gps_state->ni_notification.notify_flags             = 0; // no notification & no verification is needed
        gps_state->ni_notification.timeout                  = 0; // no timeout limit
        gps_state->ni_notification.default_response         = GPS_NI_RESPONSE_NORESP;
        gps_state->ni_notification.requestor_id[0]          = 0;
        gps_state->ni_notification.text[0]                  = 0;
        gps_state->ni_notification.requestor_id_encoding    = GPS_ENC_NONE;
        gps_state->ni_notification.text_encoding            = GPS_ENC_NONE;
        if (r->timemap.valid && r->timemap.timestamp == r->fix.timestamp)
        {
            sprintf(gps_state->ni_notification.extras, "pps=%10.10lf", r->timemap.systime);
            r->timemap.valid = 0;
        }
        else
        {
            gps_state->ni_notification.extras[0] = 0;
        }

        gps_state->ni_callbacks.notify_cb(&gps_state->ni_notification);
    }
}
#else // #ifdef ATHR_GPSNi
#   define ath_send_ni_notification(r)
#endif // #ifdef ATHR_GPSNi

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   T O K E N I Z E R                     *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

typedef struct {
    const char*  p;
    const char*  end;
} Token;

#define  MAX_NMEA_TOKENS  32

typedef struct {
    int     count;
    Token   tokens[ MAX_NMEA_TOKENS ];
} NmeaTokenizer;

static int
nmea_tokenizer_init( NmeaTokenizer*  t, const char*  p, const char*  end )
{
    int    count = 0;
    char*  q;

    // the initial '$' is optional
    if (p < end && p[0] == '$')
        p += 1;

    // remove trailing newline
    if (end > p && end[-1] == '\n') {
        end -= 1;
        if (end > p && end[-1] == '\r')
            end -= 1;
    }

    // get rid of checksum at the end of the sentecne
    if (end >= p+3 && end[-3] == '*') {
        end -= 3;
    }

    while (p < end) {
        const char*  q = p;

        q = memchr(p, ',', end-p);
        if (q == NULL)
            q = end;

        if (count < MAX_NMEA_TOKENS) {
            t->tokens[count].p   = p;
            t->tokens[count].end = q;
            count += 1;
        }

        if (q < end)
            q += 1;

        p = q;
    }

    t->count = count;
    return count;
}

static Token
nmea_tokenizer_get( NmeaTokenizer*  t, int  index )
{
    Token  tok;
    static const char*  dummy = "";

    if (index < 0 || index >= t->count) {
        tok.p = tok.end = dummy;
    } else
        tok = t->tokens[index];

    return tok;
}


static int
str2int( const char*  p, const char*  end )
{
    int   result = 0;
    int   len    = end - p;

    if (len == 0) {
      return -1;
    }

    for ( ; len > 0; len--, p++ )
    {
        int  c;

        if (p >= end)
            goto Fail;

        c = *p - '0';
        if ((unsigned)c >= 10)
            goto Fail;

        result = result*10 + c;
    }
    return  result;

Fail:
    return -1;
}

static double
str2float( const char*  p, const char*  end )
{
    int   result = 0;
    int   len    = end - p + 1;
    char  temp[32];

    if (len == 0) {
      return -1.0;
    }

    if (len >= (int)sizeof(temp))
        return 0.;

    memcpy( temp, p, len );
    temp[len] = 0;
    return strtod( temp, NULL );
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   P A R S E R                           *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

static void nmea_reader_update_utc_diff( NmeaReader*  r )
{
    time_t         now = time(NULL);
    struct tm      tm_local;
    struct tm      tm_utc;
    long           time_local, time_utc;

    gmtime_r( &now, &tm_utc );
    localtime_r( &now, &tm_local );

    time_local = tm_local.tm_sec +
                 60*(tm_local.tm_min +
                 60*(tm_local.tm_hour +
                 24*(tm_local.tm_yday +
                 365*tm_local.tm_year)));

    time_utc = tm_utc.tm_sec +
               60*(tm_utc.tm_min +
               60*(tm_utc.tm_hour +
               24*(tm_utc.tm_yday +
               365*tm_utc.tm_year)));

	r->utc_diff = time_utc - time_local;
}


static void nmea_reader_init( NmeaReader*  r )
{
    memset( r, 0, sizeof(*r) );

    r->pos      = 0;
    r->overflow = 0;
    r->utc_year = -1;
    r->utc_mon  = -1;
    r->utc_day  = -1;

    nmea_reader_update_utc_diff( r );
}

static int nmea_reader_get_timestamp(NmeaReader*  r, Token  tok, time_t *timestamp)
{
    int        hour, minute;
    double     seconds;
    struct tm  tm;
    time_t     ttime;

    if (tok.p + 6 > tok.end)
        return -1;

    if (r->utc_year < 0) {
        return -1;
    }

    hour    = str2int(tok.p,   tok.p+2);
    minute  = str2int(tok.p+2, tok.p+4);
    seconds = str2float(tok.p+4, tok.end);

    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec   = (int) seconds;
    tm.tm_year = r->utc_year - 1900;
    tm.tm_mon  = r->utc_mon - 1;
    tm.tm_mday = r->utc_day;
    tm.tm_isdst = -1;

    // D("h: %d, m: %d, s: %d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    // D("Y: %d, M: %d, D: %d", tm.tm_year, tm.tm_mon, tm.tm_mday);

	nmea_reader_update_utc_diff(r);

	ttime = mktime( &tm );
	*timestamp = ttime - r->utc_diff;

    return 0;
}

static int
nmea_reader_update_time( NmeaReader*  r, Token  tok )
{
    time_t timestamp = 0;
    int ret = nmea_reader_get_timestamp( r, tok, &timestamp);
    if (0 == ret)
        r->fix.timestamp = (long long)timestamp * 1000;
    return ret;
}

static int
nmea_reader_update_cdate( NmeaReader*  r, Token  tok_d, Token tok_m, Token tok_y )
{

    if ( (tok_d.p + 2 > tok_d.end) ||
         (tok_m.p + 2 > tok_m.end) ||
         (tok_y.p + 4 > tok_y.end) )
        return -1;

    r->utc_day = str2int(tok_d.p,   tok_d.p+2);
    r->utc_mon = str2int(tok_m.p, tok_m.p+2);
    r->utc_year = str2int(tok_y.p, tok_y.end+4);

    return 0;
}

static int
nmea_reader_update_date( NmeaReader*  r, Token  date, Token  mtime )
{
    Token  tok = date;
    int    day, mon, year;

    if (tok.p + 6 != tok.end) {

        /* no date info, will use host time in _update_time function
         */
    }
    /* normal case */
    day  = str2int(tok.p, tok.p+2);
    mon  = str2int(tok.p+2, tok.p+4);
    year = str2int(tok.p+4, tok.p+6) + 2000;

    if ((day|mon|year) < 0) {
        return -1;
    }

    r->utc_year  = year;
    r->utc_mon   = mon;
    r->utc_day   = day;

    return nmea_reader_update_time( r, mtime );
}


static double
convert_from_hhmm( Token  tok )
{
    double  val     = str2float(tok.p, tok.end);
    int     degrees = (int)(floor(val) / 100);
    double  minutes = val - degrees*100.;
    double  dcoord  = degrees + minutes / 60.0;
    return dcoord;
}


static int
nmea_reader_update_latlong( NmeaReader*  r,
                            Token        latitude,
                            char         latitudeHemi,
                            Token        longitude,
                            char         longitudeHemi )
{
    double   lat, lon;
    Token    tok;

    tok = latitude;
    if (tok.p + 6 > tok.end) {
        return -1;
    }
    lat = convert_from_hhmm(tok);
    if (latitudeHemi == 'S')
        lat = -lat;

    tok = longitude;
    if (tok.p + 6 > tok.end) {
        return -1;
    }
    lon = convert_from_hhmm(tok);
    if (longitudeHemi == 'W')
        lon = -lon;

    r->fix.flags    |= GPS_LOCATION_HAS_LAT_LONG;
    r->fix.latitude  = lat;
    r->fix.longitude = lon;
    return 0;
}


static int
nmea_reader_update_altitude( NmeaReader*  r,
                             Token        altitude,
                             Token        units )
{
    double  alt;
    Token   tok = altitude;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_ALTITUDE;
    r->fix.altitude = str2float(tok.p, tok.end);
    return 0;
}

static int
nmea_reader_update_accuracy( NmeaReader*  r,
                             Token        accuracy )
{
    double  acc;
    Token   tok = accuracy;

    if (tok.p >= tok.end)
        return -1;

    //tok is cep*cc, we only want cep
    r->fix.accuracy = str2float(tok.p, tok.end);

    if (r->fix.accuracy == 99.99){
      return 0;
    }

    r->fix.flags   |= GPS_LOCATION_HAS_ACCURACY;
    return 0;
}

static int
nmea_reader_update_bearing( NmeaReader*  r,
                            Token        bearing )
{
    double  alt;
    Token   tok = bearing;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_BEARING;
    r->fix.bearing  = str2float(tok.p, tok.end);
    return 0;
}


static int
nmea_reader_update_speed( NmeaReader*  r,
                          Token        speed )
{
    double  alt;
    Token   tok = speed;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_SPEED;
    r->fix.speed    = str2float(tok.p, tok.end);
    r->fix.speed   *= 0.514444;    // fix for Speed Unit form Knots to Meters per Second
    return 0;
}

static int
nmea_reader_update_timemap( NmeaReader* r,
                            Token       systime_tok,
                            Token       timestamp_tok)
{
    int ret;
    time_t timestamp;

    if ( systime_tok.p >= systime_tok.end ||
         timestamp_tok.p >= timestamp_tok.end)
    {
        r->timemap.valid = 0;
        return -1;
    }

    ret = nmea_reader_get_timestamp(r, timestamp_tok, &timestamp);
    if (ret)
    {
        r->timemap.valid = 0;
        return ret;
    }

    r->timemap.valid = 1;
    r->timemap.systime = str2float(systime_tok.p, systime_tok.end);
    r->timemap.timestamp = (GpsUtcTime)((long long)timestamp * 1000);
    return 0;
}


static void
nmea_reader_parse( NmeaReader*  r )
{
   /* we received a complete sentence, now parse it to generate
    * a new GPS fix...
    */
    NmeaTokenizer  tzer[1];
    Token          tok;

    if (r->pos < 9) {
         return;
    }

    if (gps_state->callbacks.nmea_cb) {
        struct timeval tv;
        unsigned long long mytimems;
        gettimeofday(&tv,NULL);
        mytimems = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        gps_state->callbacks.nmea_cb(mytimems, r->in, r->pos);
    }

    nmea_tokenizer_init(tzer, r->in, r->in + r->pos);
#ifdef GPS_DEBUG_TOKEN
    {
        int  n;
        D("Found %d tokens", tzer->count);
        for (n = 0; n < tzer->count; n++) {
            Token  tok = nmea_tokenizer_get(tzer,n);
            D("%2d: '%.*s'", n, tok.end-tok.p, tok.p);
        }
    }
#endif

    tok = nmea_tokenizer_get(tzer, 0);

    if (tok.p + 5 > tok.end) {
        /* for $PUNV sentences */
        if ( !memcmp(tok.p, "PUNV", 4) ) {
            Token tok_cfg = nmea_tokenizer_get(tzer,1);

            if (!memcmp(tok_cfg.p, "CFG_R", 5)) {
            } else if ( !memcmp(tok_cfg.p, "QUAL", 4) ) {
                Token  tok_sigma_x   = nmea_tokenizer_get(tzer, 3);

                if (tok_sigma_x.p[0] != ',') {
                    Token tok_accuracy      = nmea_tokenizer_get(tzer, 10);
                    nmea_reader_update_accuracy(r, tok_accuracy);
                }
            } else if (!memcmp(tok_cfg.p, "TIMEMAP", 7))
            {
                Token systime = nmea_tokenizer_get(tzer, 8); // system time token
                Token timestamp = nmea_tokenizer_get(tzer, 2); // UTC time token
                nmea_reader_update_timemap(r, systime, timestamp);
            }
        }else{
        }
        return;
    }

	if ( !memcmp(tok.p, "GPG", 3) ) //GPGSA,GPGGA,GPGSV
		bGetFormalNMEA = 1;
    // ignore first two characters.
    tok.p += 2;

    if ( !memcmp(tok.p, "GGA", 3) ) {
        // GPS fix
        Token  tok_fixstaus      = nmea_tokenizer_get(tzer,6);

        if (tok_fixstaus.p[0] > '0') {

          Token  tok_time          = nmea_tokenizer_get(tzer,1);
          Token  tok_latitude      = nmea_tokenizer_get(tzer,2);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,3);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,4);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,5);
          Token  tok_altitude      = nmea_tokenizer_get(tzer,9);
          Token  tok_altitudeUnits = nmea_tokenizer_get(tzer,10);

          nmea_reader_update_time(r, tok_time);
          nmea_reader_update_latlong(r, tok_latitude,
                                        tok_latitudeHemi.p[0],
                                        tok_longitude,
                                        tok_longitudeHemi.p[0]);
          nmea_reader_update_altitude(r, tok_altitude, tok_altitudeUnits);
        }

    } else if ( !memcmp(tok.p, "GLL", 3) ) {

        Token  tok_fixstaus      = nmea_tokenizer_get(tzer,6);

        if (tok_fixstaus.p[0] == 'A') {

          Token  tok_latitude      = nmea_tokenizer_get(tzer,1);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,2);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,3);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,4);
          Token  tok_time          = nmea_tokenizer_get(tzer,5);

          nmea_reader_update_time(r, tok_time);
          nmea_reader_update_latlong(r, tok_latitude,
                                        tok_latitudeHemi.p[0],
                                        tok_longitude,
                                        tok_longitudeHemi.p[0]);
        }
    } else if ( !memcmp(tok.p, "GSA", 3) ) {

        Token  tok_fixStatus   = nmea_tokenizer_get(tzer, 2);
        int i;

        if (tok_fixStatus.p[0] != '\0' && tok_fixStatus.p[0] != '1') {

          r->sv_status.used_in_fix_mask = 0ul;

          for (i = 3; i <= 14; ++i){

            Token  tok_prn  = nmea_tokenizer_get(tzer, i);
            int prn = str2int(tok_prn.p, tok_prn.end);

            /* only available for PRN 1-32 */
            if ((prn > 0) && (prn < 33)){
              r->sv_status.used_in_fix_mask |= (1ul << (prn-1));
              r->sv_status_changed = 1;
              /* mark this parameter to identify the GSA is in fixed state */
              r->gsa_fixed = 1;
            }
          }

        }else {
          if (r->gsa_fixed == 1) {
            r->sv_status.used_in_fix_mask = 0ul;
            r->sv_status_changed = 1;
            r->gsa_fixed = 0;
          }
        }
    } else if ( !memcmp(tok.p, "GSV", 3) ) {

        Token  tok_noSatellites  = nmea_tokenizer_get(tzer, 3);
        int    noSatellites = str2int(tok_noSatellites.p, tok_noSatellites.end);

        if (noSatellites > 0) {

          Token  tok_noSentences   = nmea_tokenizer_get(tzer, 1);
          Token  tok_sentence      = nmea_tokenizer_get(tzer, 2);

          int sentence = str2int(tok_sentence.p, tok_sentence.end);
          int totalSentences = str2int(tok_noSentences.p, tok_noSentences.end);
          int curr;
          int i;

          if (sentence == 1) {
              r->sv_status_changed = 0;
              r->sv_status.num_svs = 0;
          }

          curr = r->sv_status.num_svs;

          i = 0;

          while (i < 4 && r->sv_status.num_svs < noSatellites){

                 Token  tok_prn = nmea_tokenizer_get(tzer, i * 4 + 4);
                 Token  tok_elevation = nmea_tokenizer_get(tzer, i * 4 + 5);
                 Token  tok_azimuth = nmea_tokenizer_get(tzer, i * 4 + 6);
                 Token  tok_snr = nmea_tokenizer_get(tzer, i * 4 + 7);

                 r->sv_status.sv_list[curr].prn = str2int(tok_prn.p, tok_prn.end);
                 r->sv_status.sv_list[curr].elevation = str2float(tok_elevation.p, tok_elevation.end);
                 r->sv_status.sv_list[curr].azimuth = str2float(tok_azimuth.p, tok_azimuth.end);
                 r->sv_status.sv_list[curr].snr = str2float(tok_snr.p, tok_snr.end);

                 r->sv_status.num_svs += 1;

                 curr += 1;

                 i += 1;
          }

          if (sentence == totalSentences) {
              r->sv_status_changed = 1;
          }
        }

    } else if ( !memcmp(tok.p, "RMC", 3) ) {

        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,2);

        if (tok_fixStatus.p[0] == 'A')
        {
          Token  tok_time          = nmea_tokenizer_get(tzer,1);
          Token  tok_latitude      = nmea_tokenizer_get(tzer,3);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,4);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,5);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,6);
          Token  tok_speed         = nmea_tokenizer_get(tzer,7);
          Token  tok_bearing       = nmea_tokenizer_get(tzer,8);
          Token  tok_date          = nmea_tokenizer_get(tzer,9);

            nmea_reader_update_date( r, tok_date, tok_time );

            nmea_reader_update_latlong( r, tok_latitude,
                                           tok_latitudeHemi.p[0],
                                           tok_longitude,
                                           tok_longitudeHemi.p[0] );

            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
        }

    } else if ( !memcmp(tok.p, "VTG", 3) ) {

        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,9);

        if (tok_fixStatus.p[0] != '\0' && tok_fixStatus.p[0] != 'N')
        {
            Token  tok_bearing       = nmea_tokenizer_get(tzer,1);
            Token  tok_speed         = nmea_tokenizer_get(tzer,5);

            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
        }

    } else if ( !memcmp(tok.p, "ZDA", 3) ) {

        Token  tok_time;
        Token  tok_year  = nmea_tokenizer_get(tzer,4);

        if (tok_year.p[0] != '\0') {

          Token  tok_day   = nmea_tokenizer_get(tzer,2);
          Token  tok_mon   = nmea_tokenizer_get(tzer,3);

          nmea_reader_update_cdate( r, tok_day, tok_mon, tok_year );

        }

        tok_time  = nmea_tokenizer_get(tzer,1);

        if (tok_time.p[0] != '\0') {

          nmea_reader_update_time(r, tok_time);

        }


    } else {
        tok.p -= 2;
    }

    if (!gps_state->first_fix &&
        gps_state->init == STATE_INIT &&
        r->fix.flags & GPS_LOCATION_HAS_LAT_LONG) {

        ath_send_ni_notification(r);

        if (gps_state->callbacks.location_cb) {
            gps_state->callbacks.location_cb( &r->fix );
            r->fix.flags = 0;
        }

        gps_state->first_fix = 1;
    }
}

/* parse the OAP200 commands for ATHR chipsets */
static void
athr_reader_parse( char* buf, int length)
{
    int cnt;

    D("athr_reader_parse. %.*s (%d)", length, buf, length );
    if (length < 9) {
        D("ATH command too short. discarded.");
        return;
    }

    /* for NMEAListener callback */
    if (gps_state->callbacks.nmea_cb) {
        D("NMEAListener callback for ATHR... ");
        struct timeval tv;
        unsigned long long mytimems;

        gettimeofday(&tv,NULL);
        mytimems = tv.tv_sec * 1000 + tv.tv_usec / 1000;

        gps_state->callbacks.nmea_cb(mytimems, buf, length);

        D("NMEAListener callback for ATHR sentences ended... ");
    }
}

static void
nmea_reader_addc( NmeaReader*  r, int  c )
{
    int cnt;

    if (r->overflow) {
        r->overflow = (c != '\n');
        return;
    }

    if (r->pos >= (int) sizeof(r->in)-1 ) {
        r->overflow = 1;
        r->pos      = 0;
        return;
    }

    r->in[r->pos] = (char)c;
    r->pos       += 1;

    if (c == '\n') {
        gps_state_lock_fix(gps_state);
        nmea_reader_parse( r );
        gps_state_unlock_fix(gps_state);
        r->pos = 0;
    }
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       C O N N E C T I O N   S T A T E                 *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

/* commands sent to the gps thread */
enum {
    CMD_QUIT  = 0,
    CMD_START = 1,
    CMD_STOP  = 2
};


static void gps_state_update_fix_freq(GpsState *s, int fix_freq)
{

  s->fix_freq = fix_freq;
  return;

}


static void
gps_state_done( GpsState*  s )
{
    // tell the thread to quit, and wait for it
    char   cmd = CMD_QUIT;
    void*  dummy;
    int ret;

    do { ret=write( s->control[0], &cmd, 1 ); }
    while (ret < 0 && errno == EINTR);

    DFR("gps waiting for command thread to stop");
	gps_sleep(s);

    pthread_join(s->thread, &dummy);

    /* Timer thread depends on this state check */
    s->init = STATE_QUIT;
    s->fix_freq = -1;

    // close the control socket pair
    close( s->control[0] ); s->control[0] = -1;
    close( s->control[1] ); s->control[1] = -1;

    sem_destroy(&s->fix_sem);
	g_gpscallback = 0;

    memset(s, 0, sizeof(*s));

}

static int athr_run_hook(char* name)
{
    char   prop[PROPERTY_VALUE_MAX];
    char   buf[PROPERTY_VALUE_MAX + 20];
    if (property_get("athr.gps.hookspath",prop,"") == 0)
	{
        ALOGE("%s: athr.gps.hookspath property is not set", __FUNCTION__);
		return 0;
    }
    sprintf(buf,"%s/%s" , prop, name);
    ALOGI("%s: going to execute hook  \"%s\"", __FUNCTION__, buf);
    return !system(buf);
}

static int athr_run_hook_start()
{
    return athr_run_hook("start");
}

static int athr_run_hook_stop()
{
    return athr_run_hook("stop");
}


void gps_wakeup(GpsState *state)
{
	if( sleep_lock == 0) // it reset by athr_gps_start
	{
		gps_state_lock_fix(state);

		gps_opentty(state);

		if (athr_run_hook_start())
		{
			D("%s: Hook says: quit now", __FUNCTION__);
		}
		else
		{
			D("Send WAKEUP command to GPS");
			sleep_lock = time((time_t*)NULL);
			/* send $PUNV,WAKEUP command*/
			if (write(state->fd, "$PUNV,WAKEUP*2C\r\n", 17) < 0)
			{
				D("Send WAKEUP command ERROR!");
				bOrionShutdown = 1;
			}
			else
				bOrionShutdown = 0;
		}
		bGetFormalNMEA = 0;
		gps_state_unlock_fix(state);
	}
}

void gps_sleep(GpsState *state)
{
	if (athr_run_hook_stop()) {
		D("%s: Hook says: quit now", __FUNCTION__);
		gps_state_lock_fix(state);
		started = 0;
		sleep_lock = 0; // allow next wakeup command
		gps_state_unlock_fix(state);
	}
	else
	{
		gps_state_lock_fix(state);

		if(state->fd == -1)
			gps_opentty(state);

		if (write(state->fd, "$PUNV,SLEEP*7E\r\n", 16) < 0)
		{
			D("Send SLEEP command ERROR!");
			bOrionShutdown = 1;
		}
		else
			bOrionShutdown = 0;

		started = 0;
		sleep_lock = 0; // allow next wakeup command

		gps_state_unlock_fix(state);
	}
	gps_state_lock_fix(state);
	gps_closetty(state);
	gps_state_unlock_fix(state);
}

static void
gps_state_start( GpsState*  s )
{
    char  cmd = CMD_START;
    int   ret;
	DFR("%s", __FUNCTION__);

	gps_state_lock_fix(s);
	lastcmd = CMD_START;
	gps_state_unlock_fix(s);


    do { ret=write( s->control[0], &cmd, 1 ); }
    while (ret < 0 && errno == EINTR);

    if (ret != 1)
        D("%s: could not send CMD_START command: ret=%d: %s", __FUNCTION__, ret, strerror(errno));
}


static void
gps_state_stop( GpsState*  s )
{
    char  cmd = CMD_STOP;
    int   ret;

	DFR("%s", __FUNCTION__);

	gps_state_lock_fix(s);
	lastcmd = CMD_STOP;
	gps_state_unlock_fix(s);

    do
	{
		DFR("try %s", __FUNCTION__);
		ret=write( s->control[0], &cmd, 1 );
		if(ret < 0)
		{
			ALOGE("write control socket error %s", strerror(errno));
			sleep(1);
		}
	}
    while (ret < 0 && errno == EINTR);

    if (ret != 1)
        D("%s: could not send CMD_STOP command: ret=%d: %s",
          __FUNCTION__, ret, strerror(errno));
}


static int
epoll_register( int  epoll_fd, int  fd )
{
    struct epoll_event  ev;
    int                 ret, flags;

    /* important: make the fd non-blocking */
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ev.events  = EPOLLIN;
    ev.data.fd = fd;
    do {
        ret = epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fd, &ev );
    } while (ret < 0 && errno == EINTR);
    return ret;
}


static int
epoll_deregister( int  epoll_fd, int  fd )
{
    int  ret;
    do {
        ret = epoll_ctl( epoll_fd, EPOLL_CTL_DEL, fd, NULL );
    } while (ret < 0 && errno == EINTR);
    return ret;
}

/* this is the main thread, it waits for commands from gps_state_start/stop and,
 * when started, messages from the QEMU GPS daemon. these are simple NMEA sentences
 * that must be parsed to be converted into GPS fixes sent to the framework
 */

static int         epoll_ctrlfd ;
static int         epoll_nmeafd ;


static void
gps_state_thread( void*  arg )
{
    GpsState*   state = (GpsState*) arg;
    int         gps_fd     = state->fd;
    int         control_fd = state->control[1];
	epoll_ctrlfd   = epoll_create(1);
	epoll_nmeafd   = epoll_create(1);

    // register control file descriptors for polling
    epoll_register( epoll_ctrlfd, control_fd );

    D("gps thread running");

	state->tmr_thread = state->callbacks.create_thread_cb("athr_gps_tmr", gps_timer_thread, state);
	if (!state->tmr_thread)
	{
		ALOGE("could not create gps timer thread: %s", strerror(errno));
		started = 0;
		state->init = STATE_INIT;
		goto Exit;
	}

	state->nmea_thread = state->callbacks.create_thread_cb("athr_nmea_thread", gps_nmea_thread, state);
	if (!state->nmea_thread)
	{
		ALOGE("could not create gps nmea thread: %s", strerror(errno));
		started = 0;
		state->init = STATE_INIT;
		goto Exit;
	}

	started = 0;
	state->init = STATE_INIT;

    // now loop
    for (;;) {
        struct epoll_event   events[1];
        int                  ne, nevents;

        nevents = epoll_wait( epoll_ctrlfd, events, 1, -1 );
        if (nevents < 0) {
            if (errno != EINTR)
                ALOGE("epoll_wait() unexpected error: %s", strerror(errno));
            continue;
        }
        // D("gps thread received %d events", nevents);
        for (ne = 0; ne < nevents; ne++) {
            if ((events[ne].events & (EPOLLERR|EPOLLHUP)) != 0) {
                ALOGE("EPOLLERR or EPOLLHUP after epoll_wait() !?");
                goto Exit;
            }
            if ((events[ne].events & EPOLLIN) != 0) {
                int  fd = events[ne].data.fd;

                if (fd == control_fd)
                {
                    char  cmd = 255;
                    int   ret;
                    D("gps control fd event");
                    do {
                        ret = read( fd, &cmd, 1 );
                    } while (ret < 0 && errno == EINTR);

                    if (cmd == CMD_QUIT) {
                        D("gps thread quitting on demand");
                        goto Exit;
                    }
                    else if (cmd == CMD_START)
					{
                        if (!started)
						{
							NmeaReader  *reader;
							reader = &state->reader;
							nmea_reader_init( reader );
                            D("gps thread starting  location_cb=%p", state->callbacks.location_cb);
                            started = 1;
                            state->init = STATE_START;
							/* handle wakeup routine*/
							gps_wakeup(state);
                        }
						else
							D("LM already start");

                    }
                    else if (cmd == CMD_STOP) {
                        if (started) {
                            state->init = STATE_INIT;
                       }
                    }
                }
                else
                {
                    ALOGE("epoll_wait() returned unkown fd %d ?", fd);
					gps_fd = _gps_state->fd; //resign fd to gps_fd
                }
            }
        }
    }
Exit:
	{
		void *dummy;
		continue_thread = 0;
		close(epoll_ctrlfd);
		close(epoll_nmeafd);
		pthread_join(state->tmr_thread, &dummy);
		pthread_join(state->nmea_thread, &dummy);
		DFR("gps control thread destroyed");
	}
    return;
}

static void
gps_nmea_thread( void*  arg )
{
	GpsState *state = (GpsState *)arg;
	NmeaReader  *reader;
    reader = &state->reader;

	DFR("gps entered nmea thread");
	int versioncnt = 0;

   // now loop
    while (continue_thread)
	{
		char buf[512];
		int  nn, ret;
		struct timeval tv;

		while(sleep_lock == 0 && continue_thread) //don't read while sleep
			sleep(1);

		if(state->fd == -1)
		{
			GPS_STATUS_CB(state->callbacks, GPS_STATUS_SESSION_END);
			gps_opentty(state);
			sleep(1);
			continue;
		}

		if(bOrionShutdown && started) // Orion be shutdown but LM is started, try to wake it up.
		{
			ALOGI("Try to wake orion up after 5 secs");
			sleep_lock = 0;
			sleep(5);
			GPS_STATUS_CB(state->callbacks, GPS_STATUS_SESSION_BEGIN);
			gps_wakeup(state);
			nmea_reader_init( reader );
			bOrionShutdown = 0;
		}

		if(!continue_thread)
			break;

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(state->fd, &readfds);
		/* Wait up to 100 ms. */
		tv.tv_sec = 0;
		tv.tv_usec = 100;

		ret = select(state->fd + 1, &readfds, NULL, NULL, &tv);

		if (FD_ISSET(state->fd, &readfds))
		{
			memset(buf,0,sizeof(buf));
			ret = read( state->fd, buf, sizeof(buf) );
			if (ret > 0)
			{
				if (strstr(buf, "CFG_R"))
				{
					ALOGI("ver %s",buf);
				}

				for (nn = 0; nn < ret; nn++)
					nmea_reader_addc( reader, buf[nn] );

				/* ATHR in/out sentences*/
				if ((buf[0] == 'O') && (buf[1] == 'A') && (buf[2] == 'P')) {
					D("OAP200 sentences found");
					athr_reader_parse(buf, ret);
					/*
					for (nn = 0; nn < ret; nn++)
						D("%.2x ", buf[nn]);
					*/
				}else if ((buf[0] == '#') && (buf[1] == '!') && \
						  (buf[2] == 'G') && (buf[3] == 'S') && \
						  (buf[4] == 'M') && (buf[5] == 'A')) {
					D("GSMA sentences found");
					athr_reader_parse(buf, ret);
				}
			}
			else
			{
				DFR("Error on NMEA read :%s",strerror(errno));
				gps_closetty(state);
				GPS_STATUS_CB(state->callbacks, GPS_STATUS_SESSION_END);
				sleep(3); //wait Orion shutdown.
				bOrionShutdown = 1;
				continue;
			}
		}

		if(!continue_thread)
			break;
	}
Exit:
	DFR("gps nmea thread destroyed");
	return;
}

static void
gps_timer_thread( void*  arg )
{
    int need_sleep = 0;
    int sleep_val = 0;

  GpsState *state = (GpsState *)arg;

  DFR("gps entered timer thread");

  do {

	while(started != 1 && continue_thread) //
	{
		usleep(500*1000);
	}

    gps_state_lock_fix(state);

    if ((state->reader.fix.flags & GPS_LOCATION_HAS_LAT_LONG) != 0) {

      //D("gps fix cb: 0x%x", state->reader.fix.flags);

      if (state->callbacks.location_cb) {
          state->callbacks.location_cb( &state->reader.fix );
          state->reader.fix.flags = 0;
          state->first_fix = 1;
      }

      if (state->fix_freq == 0) {
        state->fix_freq = -1;
      }
    }

    if (state->reader.sv_status_changed != 0) {

      // D("gps sv status callback");

      if (state->callbacks.sv_status_cb) {
          state->callbacks.sv_status_cb( &state->reader.sv_status );
          state->reader.sv_status_changed = 0;
      }

    }

    need_sleep = (state->fix_freq != -1 && (state->init != STATE_QUIT) ? 1 : 0);
    sleep_val = state->fix_freq;

    gps_state_unlock_fix(state);

    if (need_sleep) {
        sleep(sleep_val);
    } else {
        D("won't sleep because fix_freq=%d state->init=%d",state->fix_freq, state->init);
    }
    ath_send_ni_notification(&state->reader);
	if( state->init == STATE_INIT && lastcmd == CMD_STOP && started == 1)
	{
		int gap = 0;
		D("Wait for NMEA coming,%d,%d,%d", state->init , lastcmd, started);

		while (state->init != STATE_START && bGetFormalNMEA == 0 && continue_thread && !bOrionShutdown)
		{
			usleep(300*1000);
			if (++gap > 100)
				break;
		} ;

		D("Get NMEA %d and state %d",bGetFormalNMEA,state->init);
		// even we don't get nmea after 30 second, still force close it
		bGetFormalNMEA |= (gap >= 100);

		if( state->init == STATE_INIT && lastcmd == CMD_STOP && started == 1)
		{
			gps_sleep(state);
		}
		else
		{
			D("User enter LM before sending sleep, so drop it");
		}
	}

  } while(continue_thread);


  DFR("gps timer thread destroyed");

  return;

}

static char   prop[PROPERTY_VALUE_MAX];

int gps_opentty(GpsState *state)
{

	DFR("%s", __FUNCTION__);

	if(strlen(prop) <= 0)
	{
		state->fd  = -1;
		return state->fd;
	}

	if(state->fd != -1)
		gps_closetty(state);

    do {
        state->fd = open( prop, O_RDWR | O_NOCTTY | O_NONBLOCK);
    } while (state->fd < 0 && errno == EINTR);

    if (state->fd < 0) {
        char device[256];

        // let's try add /dev/ to the property name as a last resort
        if ( snprintf(device, sizeof(device), "/dev/%s0", prop) >= (int)sizeof(device) ) {
            DFR("gps serial device name too long: '%s'", prop);
            return -1;
        }

        do {
            state->fd = open( device, O_RDWR | O_NOCTTY | O_NONBLOCK);
        } while (state->fd < 0 && errno == EINTR);

        if (state->fd < 0)
        {
            ALOGE("could not open gps serial device %s: %s", prop, strerror(errno) );
            return -1;
        }
    }

    D("gps will read from %s", prop);

    // disable echo on serial lines
    if ( isatty( state->fd ) ) {
        struct termios  ios;
        tcgetattr( state->fd, &ios );
        bzero(&ios, sizeof(ios));
        ios.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
        ios.c_iflag = IGNPAR;
        ios.c_oflag = 0;
        ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
        tcsetattr( state->fd, TCSANOW, &ios );
		tcflush(state->fd,TCIOFLUSH);
    }

	epoll_register( epoll_nmeafd, state->fd );

	return 0;
}

void gps_closetty(GpsState *s)
{
	if(s->fd != -1)
	{
		DFR("%s", __FUNCTION__);
		// close connection to the QEMU GPS daemon
		epoll_deregister( epoll_nmeafd, s->fd );
		close( s->fd );
		s->fd = -1;
	}
}

static void
gps_state_init( GpsState*  state )
{
    int    ret;
    int    done = 0;

    struct sigevent tmr_event;

    state->init       = STATE_INIT;
    state->control[0] = -1;
    state->control[1] = -1;
    state->fd         = -1;
    state->fix_freq   = -1;
    state->first_fix  = 0;
	continue_thread = 1;

    if (sem_init(&state->fix_sem, 0, 1) != 0) {
      D("gps semaphore initialization failed! errno = %d", errno);
      return;
    }

    // look for a kernel-provided device name
    if (property_get("athr.gps.mode",prop,"hosted") == 0)
    {
      DFR("Running ATHR GPS driver in hosted mode!");
      if (property_get("athr.gps.node",prop,"") == 0)
      {
        DFR("no user specific gps device name... try default name... ");
        if (property_get("ro.kernel.android.gps",prop,"") == 0)
        {
          DFR("no kernel-provided gps device name (hosted)");
          DFR("please set ro.kernel.android.gps property");
          return;
        }
      }
    }else /* not hosted mode */
    {
      if (property_get("ro.kernel.android.gps",prop,"") == 0)
      {
        DFR("no kernel-provided gps device name (not hosted)");
        DFR("please set ro.kernel.android.gps property");
        return;
      }
    }


    if ( socketpair( AF_LOCAL, SOCK_STREAM, 0, state->control ) < 0 ) {
        ALOGE("could not create thread control socket pair: %s", strerror(errno));
        goto Fail;
    }

	state->thread = state->callbacks.create_thread_cb("athr_gps", gps_state_thread, state);
        if (!state->thread)
	{
        ALOGE("could not create gps thread: %s", strerror(errno));
        goto Fail;
    }
        state->callbacks.set_capabilities_cb(GPS_CAPABILITY_SCHEDULING);
	D("gps state initialized");

    return;

Fail:
    gps_state_done( state );
}


/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       I N T E R F A C E                               *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/


static int athr_gps_init(GpsCallbacks* callbacks)
{
    GpsState*  s = _gps_state;

	D("gps state initializing %d",s->init);

    s->callbacks = *callbacks;
    if (!s->init)
        gps_state_init(s);

	if(!g_gpscallback)
		g_gpscallback = callbacks;

    return 0;
}

static void
athr_gps_cleanup(void)
{
    GpsState*  s = _gps_state;

	D("%s: called", __FUNCTION__);

    if (s->init)
        gps_state_done(s);
}

static int athr_gps_start()
{
    GpsState*  s = _gps_state;
	D("%s: called", __FUNCTION__ );

	if(gps_checkstate(s) == -1)
	{
		DFR("%s: called with uninitialized state !!", __FUNCTION__);
		return -1;
	}
	gps_state_start(s);

	GPS_STATUS_CB(s->callbacks, GPS_STATUS_SESSION_BEGIN);

    return 0;
}


static int
athr_gps_stop()
{
    GpsState*  s = _gps_state;

	D("%s: called", __FUNCTION__ );

	if(gps_checkstate(s) == -1)
	{
		DFR("%s: called with uninitialized state !!", __FUNCTION__);
		return -1;
	}

	//close LM first
	gps_state_stop(s);
	D("Try to change state to init");
	//change state to INIT
	GPS_STATUS_CB(s->callbacks, GPS_STATUS_SESSION_END);

    return 0;
}


static void
athr_gps_set_fix_frequency(int freq)
{
    GpsState*  s = _gps_state;

	if(gps_checkstate(s) == -1)
	{
		DFR("%s: called with uninitialized state !!", __FUNCTION__);
		return;
	}

    s->fix_freq = (freq <= 0) ? 1 : freq;

    D("gps fix frquency set to %d secs", freq);
}

static int
athr_gps_inject_time(GpsUtcTime time, int64_t timeReference, int uncertainty)
{
    return 0;
}

static void
athr_gps_delete_aiding_data(GpsAidingData flags)
{
}

static int
athr_gps_inject_location(double latitude, double longitude, float accuracy)
{
    return 0;
}



static int athr_gps_set_position_mode(GpsPositionMode mode, GpsPositionRecurrence recurrence,
									  uint32_t min_interval, uint32_t preferred_accuracy, uint32_t preferred_time)
{
	GpsState*  s = _gps_state;

	// only standalone is supported for now.

	if (mode != GPS_POSITION_MODE_STANDALONE)
	{
		D("%s: set GPS POSITION mode error! (mode:%d) ", __FUNCTION__, mode);
		D("Set as standalone mode currently! ");
		//        return -1;
	}

	if (!s->init) {
		D("%s: called with uninitialized state !!", __FUNCTION__);
		return -1;
	}

	s->fix_freq = min_interval/1000;
	if (s->fix_freq ==0)
	{
		s->fix_freq =1;
	}
    D("gps fix frquency set to %d sec", s->fix_freq);
    switch(s->fix_freq)
    {
        case 1:
            write(s->fd, "$PUNV,SET,28,0,0,0*cc\r\n",23);
            break;
        case 5:
            write(s->fd, "$PUNV,SET,28,5,0,5*cc\r\n",23);
            break;
        case 15:
            write(s->fd, "$PUNV,SET,28,15,0,15*cc\r\n",25);
            break;
        default:
            write(s->fd, "$PUNV,SET,28,0,0,0*cc\r\n",23);
            break;
    }
    return 0;
}


int gps_checkstate(GpsState *s)
{
    if (!s->init) {

        if(g_gpscallback)
			athr_gps_init(g_gpscallback);

		if(!s->init)
		{
			ALOGE("%s: still called with uninitialized state !!", __FUNCTION__);
			return -1;
		}
    }

	return 0;
}

#ifdef ATHR_GPSXtra
/*
 * no special parameter to be initialized here
 */
static int athr_gps_xtra_init(GpsXtraCallbacks* callbacks)
{
    D("%s: entered", __FUNCTION__);

    return 0;
}

/*
 * real nmea command injection function
 */
static int athr_gps_xtra_inject_data(char *data, int length)
{
    GpsState*  s = _gps_state;
    int cnt;

    D("%s: entered, data: %.2x (len: %d)", __FUNCTION__, *data, length);

    if (write(s->fd, data, length) < 0)
    {
      D("Send $PUNV command ERROR!");
      return -1;
    }

#ifdef GPS_DEBUG
    /* debug */
    D("inject cmds: ");
    for (cnt=0; cnt<length; cnt++)
      D("%.2x (%d) ", data[cnt], data[cnt]);
    /* end debug */
    D("%s: closed", __FUNCTION__);
#endif
    return 0;
}

static const GpsXtraInterface athrGpsXtraInterface = {
	sizeof(GpsXtraInterface),
    athr_gps_xtra_init,
    athr_gps_xtra_inject_data,
};
#endif /* ATHR_GPSXtra */

#ifdef ATHR_GPSNi
/*
 * Registers the callbacks for HAL to use
 */
static void athr_gps_ni_init(GpsNiCallbacks* callbacks)
{
    GpsState*  s = _gps_state;

    D("%s: entered", __FUNCTION__);

    if (callbacks)
    {
        s->ni_init = 1;
        s->ni_callbacks = *callbacks;
    }
}

/*
 * Sends a response to HAL
 */
static void athr_gps_ni_respond(int notif_id, GpsUserResponseType user_response)
{
    // D("%s: entered", __FUNCTION__);
}

static const GpsNiInterface athrGpsNiInterface = {
	sizeof(GpsNiInterface),
    athr_gps_ni_init,
    athr_gps_ni_respond,
};
#endif // ATHR_GPSNi

static const void* athr_gps_get_extension(const char* name)
{
    if (strcmp(name, GPS_XTRA_INTERFACE) == 0)
    {
#ifdef ATHR_GPSXtra
        D("%s: found xtra extension", __FUNCTION__);
        return (&athrGpsXtraInterface);
#endif // ATHR_GPSXtra
    }
    else if (strcmp(name, GPS_NI_INTERFACE) == 0)
    {
#ifdef ATHR_GPSNi
        D("%s: found ni extension", __FUNCTION__);
        return (&athrGpsNiInterface);
#endif // ATHR_GPSNi
    }
    D("%s: no GPS extension for %s is found", __FUNCTION__, name);
    return NULL;
}

static const GpsInterface  athrGpsInterface = {
    .size =sizeof(GpsInterface),
    .init = athr_gps_init,
    .start = athr_gps_start,
    .stop = athr_gps_stop,
    .cleanup = athr_gps_cleanup,
    .inject_time = athr_gps_inject_time,
    .inject_location = athr_gps_inject_location,
    .delete_aiding_data = athr_gps_delete_aiding_data,
    .set_position_mode = athr_gps_set_position_mode,
    .get_extension = athr_gps_get_extension,
};

const GpsInterface* gps_get_hardware_interface()
{
    return &athrGpsInterface;
}

/* for GPSXtraIterface */

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       D E V I C E                                     *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

static void gps_dev_send(int fd, char *msg)
{
  int i, n, ret;

  i = strlen(msg);

  n = 0;

  DFR("function gps_dev_send: %s", msg);
  do {

    ret = write(fd, msg + n, i - n);

    if (ret < 0 && errno == EINTR) {
      continue;
    }

    n += ret;

  } while (n < i);

  return;

}

static unsigned char gps_dev_calc_nmea_csum(char *msg)
{
  unsigned char csum = 0;
  int i;

  for (i = 1; msg[i] != '*'; ++i) {
    csum ^= msg[i];
  }

  return csum;
}
