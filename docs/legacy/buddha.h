

#include <netinet/in.h>


#define sqr( x )	({ typeof(x) __x = (x); __x * __x; })
#define mod( cpx )	sqr( (cpx).re ) + sqr( (cpx).im )


#define STEP		16
#define NAMESIZE	20
#define CHUNK		0xFFFF
#define	SKTCHUNK	CHUNK
#define SYMMETRICX	FALSE


#define PORT		30870
#define UNIX_PATH_MAX	108


#define MSG_INFO	123
#define MSG_OK		309458
#define MSG_ERROR	8932

#define PIXEL_T_MAX	0xFFFFFFFF
#define BENCH_SIZE	10000000


typedef unsigned int pixel_t;


/// @brief Struttura semplificata per memorizzare un numero complesso.
typedef struct {
	double re;
	double im;
} complex_t;


typedef unsigned char bool_t;

enum buddha_mode { NORMAL, ANTIBUDDHA, BENCHMARK };

typedef struct {
	// caratteristiche del grafico
	double maxre, maxim;
	double minre, minim;
	double scale;
	
	pixel_t* buf;
	unsigned int low;
	unsigned int high;
	unsigned int load;
	char* name;

	// questi dati vengono calcolati dai dati precedenti
	// ma sono utili per le varie funzioni
	double rangere, rangeim;
	unsigned int w;
	unsigned int h;
	unsigned int size;
	
	// caratteristiche delle iterazioni
	unsigned int points;
	unsigned int iteration;
	
	bool_t mode;
	
	char* cmd;
	char* image;
	unsigned int procs;
	unsigned int delay;
	unsigned int save;
	
	char* server;
	struct sockaddr_in sa;
} buddha_t;



typedef struct {
	buddha_t* b;
	int id;
	
} thread_data_t;




// buddha-lib.c
int 	getCommandLine		( char** out, int argc, char** argv );
int 	getArguments		( int* argc, char*** argv, char** cmd );
int 	buddha_eq		( buddha_t* a, buddha_t* b );
void 	clean 			( int exitVal, buddha_t* b );
int 	save 			( buddha_t* b );
int	savem			( buddha_t* b, unsigned char** out, int* len );
int 	load	 		( buddha_t* b );
int 	saveImage 		( buddha_t* b );
void 	printState		( buddha_t* b );
	
// buddha-parser.c
int 	parseOptions 		( int argc, char** argv, buddha_t* b, int flags );


// buddha-com.c
int 	sendMessage 		( void* src, int size, int fd );
int 	receiveMessage		( void* dest, int size, int fd );
int 	sendData 		( buddha_t* b, int fd );
int	receiveData	 	( buddha_t* b, int fd, int overwrite );
int	createServerChannel 	( struct sockaddr_in* sa, int* fd );
int	waitForConnections 	( int fd, int* cfd );
int	connectToServer 	( struct sockaddr_in* sa, int* cfd );

