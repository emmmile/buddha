/// @brief BuddhaBrot generator 
/// @author Emilio Del Tessandoro
/// @version 2.3
/// 
/// 
/// TODO: il client invia i dati compressi per risparmiare tempo, o cmq aggiungere un'opzione per fare cio'
/// TODO: non ci sono i canali, c'e' un solo array / array 3d

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
//#include <error.h>
#include <time.h>
#include <err.h>
#include <limits.h>
#include <sys/utsname.h>
#include <float.h>
#include <zlib.h>
#include <unistd.h>
#include <pthread.h>
//#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../commons/png/cpng.h"
#include "../commons/various.h"
#include "buddha.h"


//inline int (*foo) ( complex_t* seq, buddha_t* b );



/*anche questa incide parecchio...*/

inline void drawPoint ( complex_t* z, buddha_t* b ) {
	register int x, y;
	//static int count = 0;
	//static int ccount = 0;
	
	//printf( "%lf, %lf\n", z->re, z->im );
	// traslo il punto e lo scalo (converto a int che dopo e' piu' veloce)
	x = ( z->re - b->minre ) * b->scale;
	//y = ( b->maxim - z->im ) * b->scale;
	//printf( "%d, %d\n", x, y );
	if ( x >= 0 && x < b->w ) {
		// le y sono riferite al punto in alto a sinistra
		y = ( b->maxim - z->im ) * b->scale;
		
		if ( y >= 0 && y < b->h )// {
			b->buf[ y * b->w + x ]++;// count++; } else { ccount++; getchar(); }
			
		#if !SYMMETRICX
		y = ( b->maxim + z->im ) * b->scale;
		
		if ( y >= 0 && y < b->h )// {
			b->buf[ y * b->w + x ]++;// count++; } else { ccount++; getchar();}
		#endif
	}// else {
	//	ccount++;
	//	getchar();
	//}
	
	
}










int antiBuddha ( complex_t* seq, buddha_t* b ) {
	complex_t z;
	int i;
	
	
	for ( i = 0; i < b->high - 1; ) {
		//itotal++;
		if ( mod( seq[i] ) > 4.0 ) {
			i = 0;
			return i;
		}
		
		z.re = sqr( seq[i].re ) - sqr( seq[i].im ) + seq[0].re;
		z.im = 2.0 * seq[i].re * seq[i].im + seq[0].im;
		seq[++i].re = z.re;
		seq[i].im = z.im;
	}
	

	return i;	
}





inline int inside ( complex_t* c, buddha_t* b ) {
	return  c->re <= b->maxre && c->re >= b->minre && 
		c->im <= b->maxim && c->im >= b->minim;
}



inline void random_r_complex ( complex_t* seq, struct random_data* buf ) {
	int32_t rnd;
	
	random_r( buf, &rnd );
	seq->re = ( rnd << 1 ) * 9.31322575049159384821E-10;
	random_r( buf, &rnd );
	seq->im = ( rnd << 1 ) * 9.31322575049159384821E-10;
}


inline double random_r_unit_circle ( complex_t* c, struct random_data* buf ) {
	int32_t rnd;
	register double s;
	
	while ( TRUE ) {
		random_r( buf, &rnd );
		c->re = ( rnd << 1 ) * 4.656612875245796924105E-10;
		random_r( buf, &rnd );
		c->im = ( rnd << 1 ) * 4.656612875245796924105E-10;
	
		s = mod( *c );
		if ( s < 1.0 ) return s;
	}
}



#define random_r_double ( buf ) ({				\
	int32_t __rnd;						\
	random_r( buf, &__rnd );				\
	(__rnd << 1 ) * 1.0 / RAND_MAX;				\
})

#define random_r_double_neg ( buf ) ({				\
	int32_t __rnd;						\
	random_r( buf, &__rnd );				\
	__rnd * 1.0 / RAND_MAX;					\
})



void mutate ( complex_t* c, struct random_data* buf, buddha_t* b, double radius ) {
	// Gaussian mutations..
	
	/*// the EdT method
	#define GAUSSIAN_STEPS 4
	int i;
	complex_t cc;
	radius *= 5.0 / ( 3.0 * GAUSSIAN_STEPS );
	
	for ( i = 0; i < GAUSSIAN_STEPS; i++, c->re += cc.re * radius, 
					      c->im += cc.im * radius )
		random_r_complex( &cc, buf );*/
	
	// the Box-Muller method
	/*double tmp = -2.0 * log( random_r1( buf ) );
	double pmt = 2.0 * M_PI * random_r1( buf );
	c->re = sqrt( tmp ) * cos( pmt ) * radius;
	c->im = sqrt( tmp ) * sin( pmt ) * radius;*/
	
	
	// the Marsaglia polar method
	complex_t cc;
	double s = random_r_unit_circle ( &cc, buf );
	double factor = sqrt( -2.0 * log( s ) / s ) * radius;
	c->re += cc.re * factor;
	c->im += cc.im * factor;
}




double distance ( complex_t* seq, int slen, buddha_t* b ) {
	double min = 32.0;
	double dist;
	complex_t tmp;
	complex_t centre;
	int i;
	
	centre.re = b->minre + b->rangere * 0.5;
	centre.im = b->minim + b->rangeim * 0.5;
	
	for ( i = 0; i <= slen; i++ ) {
		if ( inside( seq + i, b ) )
			return 0.0;
		else {
			tmp.re = seq[i].re - centre.re;
			tmp.im = seq[i].im - centre.im;
			dist = mod( tmp );
			
			if ( dist < min ) min = dist;
		}
	}
	
	return min;
}




/** @brief Calcola la sequenza di MandelBrot.
 *  ATTENZIONE una modifica a questa funzione cambia drasticamente la velocita' del programma.
 * 
 *  La funzione genera un punto random di parte complessa positiva e poi calcola la 
 *  successione basata sull'equazione di MandelBrot. Il calcolo finisce o quando viene
 *  trovata una periodicita' nella successione (in tal caso essa rimane limitata), oppure 
 *  quando il valore assoluto dell'ultimo punto calcolato eccede 2, oppure quando sono
 *  stati calcolati b->sequenceSize punti.
 *  Modifica 12/3/2009:
 *  In seq[0] sono gia' presenti i valori iniziali sui quali iterare.
 *  @return -1 se la sequenza calcolata e' periodica
 *  @return >= 0 se la sequenza scappa all'infinito
 */
int buddha ( complex_t* seq, buddha_t* b ) {
	complex_t z;
	int i, critical = STEP;
	
	for ( i = 0; i < b->high - 1; ) {
		if ( mod( seq[i] ) > 4.0 ) 
			// piu' lento ma visivamente piu' bello
			if ( !inside( seq + i, b ) )
				return i;// - 1;
		
		z.re = sqr( seq[i].re ) - sqr( seq[i].im ) + seq[0].re;
		z.im = 2.0 * seq[i].re * seq[i].im + seq[0].im;
		//z.re = sqr( sqr( seq[i].re ) ) + sqr( sqr( seq[i].im ) ) - 6.0 * sqr( seq[i].re ) * sqr( seq[i].im )+ seq[0].re;
		//z.im = 4.0 * ( seq[i].im * sqr( seq[i].re ) * seq[i].re -seq[i].re * sqr( seq[i].im ) * seq[i].im )+ seq[0].im;
		seq[++i].re = z.re;
		seq[i].im = z.im;
		

		if ( i > critical ) {
			if ( fabs( z.re - seq[critical].re ) < fabs( z.re ) * FLT_EPSILON && 
			     fabs( z.im - seq[critical].im ) < fabs( z.im ) * FLT_EPSILON )
				return -1;
			
			if ( i == critical * 2 ) critical *= 2;
		}
	}
	

	return -1;
}













/** @brief Esegue i calcoli
 * 
 *  Inizializza il generatore dei numeri random (in quanto viene chiamata da ogni
 *  thread), calcola le sequenze e infine le disegna sul grafico.
 */
void* uniform_iterate ( void* arg ) {
	buddha_t* b = ((thread_data_t*) arg)->b;
	int id = ((thread_data_t*) arg)->id;
	
	complex_t* seq = calloc(  b->high, sizeof( complex_t ) );
	unsigned int l, i, iterations = b->points / b->iteration, seed;
	int maxIndex = 0, j;
	struct random_data buf;
	char statebuf [256];
	double t = 0.0;
	
	// random seed generation
	//seed = time( NULL ) & 0xFFFF * ( id + 1 ) * getpid( );
	seed = id + 1;
	
	
	// start the timer
	tttime( &t );
	buf.state = (int32_t*) statebuf; // XXX this fixes the segfault
	initstate_r( seed, statebuf, sizeof( statebuf ), &buf );
	
	int scount, goods = 0;
	for ( i = 0; i < iterations && b->mode != BENCHMARK; i++ ) {
		for ( l = 0; l < b->iteration; l++ ) {
			// generate a new starting point
			random_r_complex( seq, &buf );
			
			maxIndex = buddha( seq, b );
			//maxIndex = peterDeJong( seq, b );
			for ( j = 0, scount = 0; j <= maxIndex; j++ )
				scount += inside( seq + j, b );
			
			if ( scount )
				goods++;
			
			
			//if ( maxIndex >= 0 ) maxIndex = 0;
			for ( j = 0; j <= maxIndex && j >= b->low && j < b->high; j++ )
				drawPoint( seq + j, b );
		}
		
		printf ( "  Calculated %d points...\r", i * b->iteration );
		fflush ( stdout );
	}
	
	printf( "  [Thread %d] Finished in %lf seconds.\n", id, tttime( &t ) );
	free( seq );
	printf( "Goods: %d\n", goods );
	
	
	return NULL;
}






void* non_uniform_iteratee ( void* arg ) {
	buddha_t* b = ((thread_data_t*) arg)->b;
	int id = ((thread_data_t*) arg)->id;
	
	complex_t* s = calloc( b->high, sizeof( complex_t ) );
	complex_t* t = calloc( b->high, sizeof( complex_t ) );
	unsigned int l, i, iterations = b->points / b->iteration, seed;
	int smax = 0, tmax = 0, j;
	struct random_data buf;
	char statebuf [256];
	double tt = 0.0;
	//int32_t rnd;
	
	// random seed generation
	seed = time( NULL ) & 0xFFFF * ( id + 1 ) * getpid( );
	//seed = id + 1;
	
	
	// start the timer
	tttime( &tt );
	buf.state = (int32_t*) statebuf; // XXX this fixes the segfault
	initstate_r( seed, statebuf, sizeof( statebuf ), &buf ); 
	
	
	int scount; int tcount;
	for ( i = 0; i < iterations; i++ ) {
		for ( l = 0; l < b->iteration; l++ ) {
			/* XXX Mutation algorithm test 
			s[0].re = s[0].im = 0.0;
			mutate( s, &buf, b, 0.1 );
			for ( j = 0; j <= 0 && j >= b->low && j < b->high; j++ )
				drawPoint( s + j, b ); */
				
			
			// genero una sequenza a caso
			random_r_complex( s, &buf );
			
			smax = buddha( s, b );
			if ( smax < 0 ) continue;
			
			
			// conto quanto e' "buona" questa sequenza, cioe' quanti punti della sequenza
			// vanno a finire nell'area del grafico
			for ( j = 0, scount = 0; j <= smax; j++ )
				scount += inside( s + j, b );
				
			//double goodness = count / (double) ( smax + 1 );
			if ( scount <= 0 ) continue;
			
			
			// intanto la disegno
			for ( j = 0; j <= smax && j >= b->low && j < b->high; j++ )
				drawPoint( s + j, b );
			
			
			double radius = 10.0 / b->scale;
			int bad = 0;
			while ( bad < 3 ) {
				// se il punto genera una lunga sequenza di punti
				// cerco di generare altri punti vicini a lui con sequenze lunghe
				t[0].re = s[0].re;
				t[0].im = s[0].im;
				mutate( t, &buf, b, radius );
				tmax = buddha( t, b );
			
				//if ( tmax < 0 ) break;
				
				for ( j = 0, tcount = 0; j <= tmax; j++ )
					tcount += inside( t + j, b );
			
				double qst = ( 1 + scount ) / (double) ( 1 + tcount );
				double qts = ( 1 + tcount ) / (double) ( 1 + scount );
				//double qst = scount / (double) tcount;
				//double qts = tcount / (double) scount;
			
				//printf( "slen: %d\ntlen: %d\n", scount, tcount );
				//printf( "Transition s->t: %lf\n", qst );
				//printf( "Transition t->s: %lf\n", qts );
			
				//double alpha = qst / qts;
				if ( qts >= qst ) {
					//bad = 0;
					s[0].re = t[0].re;
					s[0].im = t[0].im;
					
					//printf( "disegno %d punti\n", tmax );
					//for ( j = 0; j <= tmax && j >= b->low && j < b->high; j++ )
					//	drawPoint( t + j, b );
				} else {
					radius *= 2.0;
					bad++;
				}
				//getchar();
				for ( j = 0; j <= tmax && j >= b->low && j < b->high; j++ )
					drawPoint( t + j, b );
			}
			
			//getchar();
			
		}
		
		printf ( "  Calculated %d points...\r", i * b->iteration );
		fflush ( stdout );
	}
	
	
	printf( "  [Thread %d] Finished in %lf seconds.\n", id, tttime( &tt ) );
	free( s );
	free( t );
	
	return NULL;
}










void* non_uniform_iterate ( void* arg ) {
	buddha_t* b = ((thread_data_t*) arg)->b;
	int id = ((thread_data_t*) arg)->id;
	
	complex_t* s = calloc( b->high, sizeof( complex_t ) );
	complex_t* t = calloc( b->high, sizeof( complex_t ) );
	unsigned int l, i, iterations = b->points / b->iteration, seed;
	int smax = 0, tmax = 0, j;
	struct random_data buf;
	char statebuf [256];
	double tt = 0.0;
	//int32_t rnd;
	
	// random seed generation
	seed = time( NULL ) & 0xFFFF * ( id + 1 ) * getpid( );
	//seed = id + 1;
	
	
	// start the timer
	tttime( &tt );
	buf.state = (int32_t*) statebuf; // XXX this fixes the segfault
	initstate_r( seed, statebuf, sizeof( statebuf ), &buf ); 
	
	unsigned long long int mid = 0;
	unsigned long long int midp = 0;
	
	double dist = 16.0; double newdist;
	double rrange = 0.5 * ( b->rangere + b->rangeim );
	double radius = 0.16 * rrange;
	int scount; //int tcount;
	for ( i = 0; i < iterations; i++ ) {
		for ( l = 0; l < b->iteration; l++ ) {
			// genero una sequenza a caso
			//random_r_complex( s, &buf );
			//smax = buddha( s, b );
			//if ( smax < 0 ) continue;
			
			
			
			
			dist = 16.0;
			complex_t good; good.re = good.im = 0.0;
			int n, attempts;
			for( n = attempts = 0; dist != 0.0 && attempts < 256; attempts++, n++ ) {
				
				s[0] = good;
				mutate( s, &buf, b, 0.5 * sqrt(dist) );
				smax = buddha( s, b );
				//getchar();
				
				
				newdist = distance( s, smax, b );
				//printf( "Trovata sequenza a %lf (attuale %lf)\n", newdist, dist );
				//getchar();
				if ( newdist < dist ) {
					dist = newdist;
					good = s[0];
					//printf( "Aggiornata\n" );
				}
			}
			if ( attempts == 512 ) {
				l--;
				continue;
			}
			
			
			mid += n;
			
			//printf( "Trovata sequenza con %d iterazioni\n", nnn );
			//getchar();
			
			
			// conto quanto e' "buona" questa sequenza, cioe' quanti punti della sequenza
			// vanno a finire nell'area del grafico
			for ( j = 0, scount = 0; j <= smax; j++ )
				scount += inside( s + j, b );
			
			midp += scount;
			//printf( "%d\n", scount );
			//double goodness = count / (double) ( smax + 1 );
			if ( scount == 0 ) continue;
			
			// intanto la disegno
			for ( j = 0; j <= smax && j >= b->low && j < b->high; j++ )
				drawPoint( s + j, b );
			
			//double rrange = 0.5 * ( b->rangere + b->rangeim );
			//double radius = 0.16 * rrange;
			
			register int times;
			// times 10000, radius > 20 !!!
			for ( times = 0; times < 20000; times++ ) {
				// se il punto genera una lunga sequenza di punti
				// cerco di generare altri punti vicini a lui con sequenze lunghe
				t[0].re = s[0].re;
				t[0].im = s[0].im;
				mutate( t, &buf, b, radius );
				tmax = buddha( t, b );
				
				//double ddd = distance( t, tmax, b );
				//printf( "Distanza: %lf (%d), inside = %d\n", ddd, ddd == 0.0, scount );
				
				
				for ( j = 0; j <= tmax && j >= b->low && j < b->high; j++ )
					drawPoint( t + j, b );
			}
		}
		
		printf ( "  Calculated %d points...\r", ( i + 1 ) * b->iteration );
		fflush ( stdout );
	}
	
	printf( "Media iterazioni eseguite prima di trovare un punto buono: %d\n", (int) ( mid / b->points ) );
	printf( "Numero di punti nell'immagine (bonta' generatore): %Ld\n", midp );
	
	printf( "  [Thread %d] Finished in %lf seconds.\n", id, tttime( &tt ) );
	free( s );
	free( t );
	
	return NULL;
}




/** @brief Modalita' benchmark, un po' grezzotta per ora */
void benchmark ( buddha_t* b, complex_t* seq ) {
	unsigned int l;
	
	b->high = 2000;
	for ( l = 0; l < BENCH_SIZE; l++ )
		buddha( seq, b );
}









/// @brief Pulisce l'heap del processo
void cleanClient ( int exitVal, void* v ) {
	buddha_t* b = (buddha_t*) v;
	
	clean( exitVal, b );
}









/// Cerca di connettersi al server e invia i dati, poi chiude il canale
int sendToServer ( buddha_t* b, int port ) {
	int cfd, i = 0;
	
	if ( !b ) return errno = EINVAL;
	if ( b->mode == BENCHMARK ) return 0;
	
	
	// set client parameters
	bzero( &b->sa, sizeof( struct sockaddr_in ) );
	b->sa.sin_family = AF_INET;
	inet_aton( b->server, &b->sa.sin_addr );
	b->sa.sin_port = htons( port );
	
	
	printf( "  Sending data to '%s'...", b->server ); fflush( stdout );
	while ( ++i < 10 ) {
		if ( ( connectToServer( &b->sa, &cfd ) ) == 0 ) {			
			if ( sendData( b, cfd ) )
				printf( " %s [sendData()].\n", strerror( errno ) );
			else	printf( " Data sent.\n" );
			
			close( cfd );
			return 0;
		} else 	printf( " %s [connectToServer()].\n", strerror( errno ) );
		
		sleep( b->delay );
	}
	
	
	// se arrivo fino qui vuol dire che nn ho trovato il server
	return errno = ETIMEDOUT;
}








/*
/// crea un canale di ascolto e poi raccatta i dati dai client
int parentActions ( buddha_t* b, int port ) {
	int sfd, i, cfd;
	
	if ( !b || ( b && b->bench ) )
		return errno = EINVAL;
	
	if ( b->procs > 1 ) {
		// set server parameters
		bzero( &(b->sa), sizeof( struct sockaddr_in ) );
		b->sa.sin_family = AF_INET;
		b->sa.sin_addr.s_addr = INADDR_ANY;
		b->sa.sin_port = htons( port );
		
		if ( createServerChannel( &b->sa, &sfd ) != 0 )
			error( EXIT_FAILURE, errno, "parentActions()" );
		
		
		printf( "  Waiting for connections of local clients...\n" );
		for ( i = 0; i < b->procs; i++ ) {
			if ( waitForConnections( sfd, &cfd ) == 0 ) {
				printf( "    Receiving channels from local client...\n" );
			
				if ( receiveData( b, cfd, FALSE ) )
					printf( "    Error receiving data.\n" );
				else	printf( "    Received successfully.\n" );
			
				close( cfd );
			} else 	error( 0, errno, "waitForConnections()" );
		}
	
		close( sfd );
	}
	
	// poi esegue un salvataggio dei dati locali
	save( b );
	saveImage( b );


	
	// nel caso sia specificato anche un server remoto, invio i dati ad esso
	// comportandomi come un client
	if ( b->server )
		childActions( b, PORT );
		
	return 0;
}
*/



//#include <linux/sched.h>



int main ( int argc, char** argv ) {
	buddha_t b; bzero( &b, sizeof( buddha_t ) );
	int i, j;

	
	on_exit( cleanClient, &b );
	
	
	// parsing opzioni
	if ( parseOptions( argc, argv, &b, 0 ) )
		error( EXIT_FAILURE, errno, "main()" );	
	
	
	// alloco la memoria del blocco "provvisorio"
	if ( !( b.buf = calloc( b.size, 1 ) ) )
		perror( "main()" );
		
	// se necessario carico i dati da file
	if ( b.load && load( &b ) )
		perror( "load()" );
	
	
	// alloco memoria per i dati
	pthread_t t [b.procs];
	buddha_t tb [b.procs];
	thread_data_t td [b.procs];
	
	
	printf( "  Real range: %lf, Imaginary range: %lf\n", b.rangere, b.rangeim );
	printf( "  The resulting Image will be %dx%d pixels.\n", b.w, b.h );
	printf( "  Calculating %d points...\n", b.points );
	

	//b.procs = 1;
	//fork();
	
	
	memcpy( tb, &b, sizeof( buddha_t ) );
	tb[0].buf = b.buf;
	// creo tanti buddha e ci faccio i vari calcoli
	// poi li sommo su uno solo da questo thread
	for ( i = 0; i < b.procs; i++ ) {
		if ( i ) {
			// copio i dati del thread principale negli altri
			memcpy( tb + i, tb, sizeof( buddha_t ) );
			tb[i].buf = calloc( tb[0].size, 1 );
			memcpy( tb[i].buf, tb[0].buf, tb[0].size );
		}
		td[i].b = tb + i;
		td[i].id = i;
		
		
		//printState( td[i].b ); getchar();
		// avvio il thread
		pthread_create( t + i, NULL, uniform_iterate, (void*) (td + i) );
		printf( "  [Thread %d] Started.\n", i );
		
		//if ( b.mode == BENCHMARK ) break;
	}
	
	
	// aspetto che i thread serventi terminino
	for ( i = 0; i < b.procs; i++ ) {
		pthread_join( t[i], NULL );
		//printf( "  [Thread %d] Terminated.\n", i );
	}
	
	// raccatto (per ora grezzamente) la roba dai serventi
	for ( i = 1; i < b.procs; i++ ) {
		for ( j = 0; j < tb[0].w * tb[0].h; j++ ) 
			tb[0].buf[j] += tb[i].buf[j];
			
		free( tb[i].buf );
	}
	
	
	// infine salvo
	if ( tb[0].save ) {
		save( tb );
		saveImage( tb );
	}
	
	
	// e se e' definito un server remoto invio al server
	if ( tb[0].server )
		sendToServer( tb, PORT );
	
	
	exit( EXIT_SUCCESS );
}



#if 0

complex_t a;
complex_t d;

a.re = 1.4;
a.im = -2.3;
d.re = 2.4;
d.im = -2.1;



/// XXX Test
inline int peterDeJong ( complex_t* seq, buddha_t* b ) {	
	int i;
	
	seq[0].re = frand2();
	seq[0].im = frand2();
	
	for ( i = 0; i < ( b->seqsize - 1 ); i++ ) {
		seq[i+1].re = sin( a.re * seq[i].im ) - cos( a.im * seq[i].re );
		seq[i+1].im = sin( d.re * seq[i].re ) - cos( d.im * seq[i].im );
		
		//printf( "%lf %lf\n", a.re, a.im );
		//printf( "%lf %lf\n", d.re, d.im );
		//printf( "%lf %lf\n", seq[i+1].re, seq[i + 1].im );
		//getchar();
	}
	
	return i;
}

#endif
