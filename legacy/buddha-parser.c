

#include <argp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include "buddha.h"
#include "../commons/various.h"
#include <math.h>


extern char* program_invocation_name;



// struttura per gli argomenti delle varie opzioni
struct arguments {
	char* server;
	char* points;
	char* iteration;
	char* scale;
	char* output;
	char* low;
	char* high;
	char* maxre;
	char* maxim;
	char* minre;
	char* minim;
	char* input;
	char* threads;
	char* delay;
	int bench;
	int load;
	int save;
	int anti;
};



const char *argp_program_version = "buddha 2.1";
const char *argp_program_bug_address = "<emilio.deltessa@gmail.com>";

// documentazione
static char doc[] =
	"BuddhaBrot Generator by Emilio Del Tessandoro, a program for drawing fractals."
	"\vA CHANNEL can be a file or an expression of the form 'beg:end:name'. If the "
	"file exists the program will load it otherwise will be created a new channel "
	"as specified.";

// descrizione delle opzioni, forse un giorno le aggiungero'
static char args_doc[] = "";

// le opzioni
static struct argp_option options[] = {
	{ "server",		'S',	"SERVER", 	0,	"Specifies a server" },
	{ "points",		'p',	"INT", 		0,	"Specifies the number of points to test" },
	{ "iteration-size",	'i', 	"INT", 		0, 	"Set the iteration size to display" },
	{ "scale",		's', 	"INT", 		0,	"Set the scale value" },
	{ "output", 		'o', 	"FILE", 	0, 	"Output image name" },
	{ "benchmark", 		'b', 	0, 		0, 	"Execute in benchmark mode" },
	{ "max", 		'N', 	"INT", 		0, 	"Specifies the maximum bailout" },
	{ "min", 		'n', 	"INT", 		0,	"Specifies the minimum bailout" },
	{ "maxre", 		'R', 	"REAL", 	0, 	"mh" },
	{ "maxim", 		'M', 	"REAL", 	0, 	"mh" },
	{ "minre", 		'r', 	"REAL", 	0, 	"mh" },
	{ "minim", 		'm', 	"REAL", 	0, 	"mh" },
	{ "load", 		'l', 	"FILE", 	0, 	"mh" },
	{ "threads", 		't', 	"INT or auto", 	0, 	"mh" },
	{ "delay", 		'd', 	"INT", 		0, 	"mh" },
	{ "nosave", 		'z', 	0, 		0, 	"mh" },
	{ "anti", 		'a', 	0, 		0, 	"mh" },
	{ 0 }
};




// effettua il parsing di un'opzione
static error_t parse_opt ( int key, char *arg, struct argp_state* state ) {
	struct arguments* args = state->input;

	switch ( key ) {
		case 'a': { args->anti = TRUE; break; } 
		case 'z': { args->save = FALSE; break; }
		case 'd': { args->delay = arg; break; }
		case 'l': { args->input = arg; args->load = TRUE; break; }
		case 't': { args->threads = arg; break; }
		case 'M': { args->maxim = arg; break; }
		case 'm': { args->minim = arg; break; }
		case 'R': { args->maxre = arg; break; }
		case 'r': { args->minre = arg; break; }
		case 'N': { args->high = arg; break; }
		case 'n': { args->low = arg; break; }
		case 's': { args->scale = arg; break; }
		case 'o': { args->output = arg; break; }
		case 'i': { args->iteration = arg; break; }
		case 'b': { args->bench = TRUE; args->save = FALSE; break; }
		case 'S': { args->server = arg; break; }
		case 'p': { args->points = arg; break; }
		case ARGP_KEY_ARG: { argp_usage( state ); break; }
		case ARGP_KEY_END: { break; }
		default: { return ARGP_ERR_UNKNOWN; }
	}
	
	return 0;
}

     
     
     
     


// si occupa di assegnare al buddha_t le opzioni gia' lette da file o da linea di comando
static int setOptions ( struct arguments* a, buddha_t* b ) {
	float factor;
	
	
	#if SYMMETRICX
	factor = 0.5;
	#else
	factor = 1.0;
	#endif
	// setto le altre variabili
	b->name = a->input;
	b->high = strtol( a->high, NULL, 10 );
	b->low = strtol( a->low, NULL, 10 );
	b->maxre = strtod( a->maxre, NULL );
	b->maxim = strtod( a->maxim, NULL );
	b->minre = strtod( a->minre, NULL );
	b->minim = strtod( a->minim, NULL );
	b->server = a->server;
	b->image = a->output;
	b->iteration = strtol( a->iteration, NULL, 10 );
	b->points = strtol( a->points, NULL, 10 );
	b->scale = strtod( a->scale, NULL );
	b->rangere = b->maxre - b->minre;
	b->rangeim = b->maxim - b->minim;
	b->w = (int) ceil( b->rangere * b->scale );
	b->h = (int) ceil( b->rangeim * b->scale * factor );
	b->size = b->w * b->h * sizeof( pixel_t );
	b->load = a->load;
	b->delay = strtol( a->delay, NULL, 10 );
	b->save = a->save;
	b->mode = a->bench ? BENCHMARK : a->anti ? ANTIBUDDHA : NORMAL;
	
	
	if ( !strcmp( a->threads, "auto" ) )
		b->procs = getProcsNum( );
	else 	b->procs = strtol( a->threads, NULL, 10 );

	return 0;
}





int parseOptions ( int argc, char** argv, buddha_t* b, int flags ) {
	int r;
	struct arguments a;
	bzero( &a, sizeof( struct arguments ) );
	
	struct argp argp = { options, parse_opt, args_doc, doc };



	// defaults
	a.low = "0";
	a.high = "2048";
	a.points = "10000000";
	a.iteration = "100000";
	a.scale = "256";
	a.output = "image.png";
	a.bench = FALSE;
	a.save = TRUE;
	a.maxre = "2.0";
	a.minre = "-2.0";
	a.maxim = "1.5";
	a.minim = "-1.5";
	a.input = "data.z";
	a.threads = "auto";
	a.delay = "1";
	
	
	//progName = argv[0];
	if ( ( r = argp_parse( &argp, argc, argv, flags, 0, &a ) ) )
		return r;
	
	getCommandLine( &(b->cmd), argc, argv );
	return setOptions( &a, b );
}




