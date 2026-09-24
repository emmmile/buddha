


#include "buddha.h"
#include <zlib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <errno.h>
#include <error.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "../commons/various.h"







/** @brief invia un messaggio su una socket
 *
 * @param src dove verra' scritto il messaggio
 * @size la dimensione da scrivere in bytes
 * @fd il file descriptor
 * 
 * @return 0 su successo
 */
int sendMessage ( void* src, int size, int fd ) {
	int sent = 0;
	int chunkSize, r;
	
	do {
		//chunkSize = size - sent < SKTCHUNK ? size - sent : SKTCHUNK;
		chunkSize = size < SKTCHUNK ? size : SKTCHUNK;
		r = write( fd, src + sent, chunkSize );
		sent += r;
		size -= r;
	} while ( size > 0 && r != 0 && r != -1 );
	
	
	
	return size;
}




/** @brief riceve un messaggio da una socket
 *
 * @param src dove verra' letto il messaggio
 * @size la dimensione da leggere in bytes
 * @fd il file descriptor
 * 
 * @return 0 su successo
 */
int receiveMessage ( void* dest, int size, int fd ) {
	int received = 0;
	int chunkSize, r;
	
	do {
		//chunkSize = size - received < SKTCHUNK ? size - received : SKTCHUNK;
		chunkSize = size < SKTCHUNK ? size : SKTCHUNK;
		r = read( fd, dest + received, chunkSize );
		received += r;
		size -= r;
	} while ( size > 0 && r != 0 && r != -1 );

	
	
	return size;
}









/** @brief Si occupa di inviare tutti i dati del buddha al server
 *  
 * 1) Invia il buddha_t
 * 2) Aspetta un ACK
 * 5) Invia i dati
 *
 * @param fd la socket
 * @return 0 se l'invio e' andato a buon fine
 * @return ECOMM se la comunicazione e' fallita
 */
int sendData ( buddha_t* b, int fd ) {
	/*int msg;
	int len;
	unsigned char* data;

	
	savem( b, &data, &len );
	
	
	// invio il buddha_t e aspetto l'ack
	if ( sendMessage( (void*) b, sizeof( buddha_t ), fd ) )
		return errno = ECOMM;
	if ( receiveMessage( (void*) &msg, sizeof( int ), fd ) )
		return errno = ECOMM;
	if ( msg == MSG_ERROR )
		return errno = ECOMM;
	
	// invio la lunghezza della linea di comando e la linea di comando
	msg = strlen( b->cmd ) + 1;
	if ( sendMessage( (void*) &msg, sizeof( int ), fd ) )
		return errno = ECOMM;
	if ( sendMessage( (void*) b->cmd, msg, fd ) )
		return errno = ECOMM;
	
	
	// invio la lunghezza dei dati compressi e i dati compressi
	len -= sizeof( int ) + msg;
	if ( sendMessage( (void*) &len, sizeof( int ), fd ) )
		return errno = ECOMM;
	if ( sendMessage( (void*) ( data + sizeof( int ) + msg ), len, fd ) )
		return errno = ECOMM;*/
	int msg;
		
	if ( sendMessage( (void*) b, sizeof( buddha_t ), fd ) )
		return errno = ECOMM;
	
	if ( receiveMessage( (void*) &msg, sizeof( int ), fd ) )
		return errno = ECOMM;
	
	if ( msg == MSG_ERROR )
		return errno = ECOMM;
	
	msg = strlen( b->cmd ) + 1;
	if ( sendMessage( (void*) &msg, sizeof( int ), fd ) )
		return errno = ECOMM;
	
	if ( sendMessage( (void*) b->cmd, msg, fd ) )
		return errno = ECOMM;
	
	double t; tttime( &t );
	// tutto molto tranquillo, invio i dati dell'istogramma
	if ( sendMessage( (void*) b->buf, b->size, fd ) )
		return errno = ECOMM;
	printf( "%lf\n", b->size / tttime( &t ) / 1024.0 / 1024.0 );
	
	return 0;
}








/** @brief Si occupa di ricevere tutti i dati di un client
 *  
 * 1) Riceve il buddha_t
 * 2) Invia un ACK
 * 5) Riceve i dati
 *
 * @param fd la socket
 * @return 0 se la ricezione e' andata a buon fine
 * @return ECOMM se la comunicazione e' fallita
 */
int receiveData ( buddha_t* b, int fd, int overwrite ) {
	/*buddha_t tmp;
	int i, msg, len;
	unsigned char* compressed;
	
	
	// ricevo il buddha_t e invio l'ack
	if ( receiveMessage( (void*) &tmp, sizeof( buddha_t ), fd ) )
		return errno = ECOMM;
	// se non devo sovrascrivere b allora guardo che i dati siano uguali
	if ( !overwrite )
		msg = buddha_eq( &tmp, b ) ? MSG_OK : MSG_ERROR;
	if ( sendMessage( (void*) &msg, sizeof( int ), fd ) )
		return errno = ECOMM;
	
	
	
	// ricevo la lunghezza della linea di comando e la linea di comando
	if ( receiveMessage( (void*) &i, sizeof( int ), fd ) )
		return errno = ECOMM;
	
	tmp.cmd = calloc( 1, i );
	if ( receiveMessage( (void*) tmp.cmd, i, fd ) )
		return errno = ECOMM;
	
	
	if ( overwrite ) {
		memcpy( b, &tmp, sizeof( buddha_t ) );
		// nel caso abbia sovrascritto devo rimettere qualocsa qui
		// TODO
		b->name = "/tmp/data.z";
		b->image = "/tmp/preview.png";
		b->server = NULL;
	}
	
	
	// ricevo la lunghezza dei dati compressi e i dati compressi
	if ( receiveMessage( (void*) &len, sizeof( int ), fd ) )
		return errno = ECOMM;
	
	compressed = malloc( len );
	if ( receiveMessage( (void*) compressed, len, fd ) )
		return errno = ECOMM;
	
	
	// decomprimo
	tmp.buf = malloc( b->size );
	uLongf ssize = b->size;
	uncompress( (unsigned char*) tmp.buf, &ssize, compressed, len );
	free( compressed );
	
	// sommo il canale con quello esistente (!overwrite)
	for ( i = 0; !overwrite && i < b->w * b->h; b->buf[i] += tmp.buf[i], i++ );
	
	// se overwrite == TRUE sostituisco il canale
	if ( overwrite )
		b->buf = tmp.buf;
	// altrimenti ho gia' sommato a quello esistente e posso liberare questo
	else	free( tmp.buf );*/
	
	buddha_t tmp;
	int i, msg;
	
	
	
	if ( receiveMessage( (void*) &tmp, sizeof( buddha_t ), fd ) )
		return errno = ECOMM;
	
	// se non devo sovrascrivere b allora guardo che i dati siano uguali
	if ( !overwrite )
		msg = buddha_eq( &tmp, b ) ? MSG_OK : MSG_ERROR;
	
	// invio l'hack ed eventualmente esco
	if ( sendMessage( (void*) &msg, sizeof( int ), fd ) )
		return errno = ECOMM;
	
	// ricevo la lunghezza della linea di comando
	if ( receiveMessage( (void*) &i, sizeof( int ), fd ) )
		return errno = ECOMM;
	
	tmp.cmd = calloc( 1, i );
	// ricevo la linea di comando
	if ( receiveMessage( (void*) tmp.cmd, i, fd ) )
		return errno = ECOMM;
	
	
	if ( overwrite ) {
		memcpy( b, &tmp, sizeof( buddha_t ) );
		// nel caso abbia sovrascritto devo rimettere qualocsa qui
		// TODO
		b->name = "data.z";
		b->image = "preview.png";
		b->server = NULL;
	}
	
	
	// tutto tranquillo ricevo normalmente l'istogramma
	tmp.buf = malloc( b->size );
	
	if ( receiveMessage( (void*) tmp.buf, b->size, fd ) )
		return errno = ECOMM;
	
	// sommo il canale con quello esistente (!overwrite)
	for ( i = 0; !overwrite && i < b->w * b->h; b->buf[i] += tmp.buf[i], i++ );
	
	// se overwrite == TRUE sostituisco il canale
	if ( overwrite )
		b->buf = tmp.buf;
	// altrimenti ho gia' sommato a quello esistente e posso liberare questo
	else	free( tmp.buf );
	
	return 0;
}














/** @brief crea una socket di benvenuto su cui poi ascoltare le connessioni dei client
 * 
 * @param sa la struttura che identifica la socket
 * @param sfd dove verra' salvato il file descriptor della socket
 *
 * @return 0 se la socket e' stata creata con successo
 * @return E* se la socket() o la bind() falliscono
 */
int createServerChannel ( struct sockaddr_in* sa, int* sfd ) {
	
	if ( !sa || !sfd )
		return EINVAL;

	if ( ( *sfd = socket( sa->sin_family, SOCK_STREAM, 0 ) ) == -1 )
		return errno;
	
	//strncpy( sa.sun_path, name, UNIX_PATH_MAX );
	//sa.sun_family = AF_UNIX;
	if ( bind( *sfd, (struct sockaddr*) sa, sizeof( *sa ) ) == -1 )
		return errno;
	
	return 0;
}










/** @brief aspetta le connessioni dei client
 * 
 * @param fd il file descriptor della socket su cui ascoltare
 * @param cfd dove verra' salvato il file descriptor della socket di comunicazione
 *
 * @return 0 se la connessione e' stata stabilita con successo
 * @return E* se la listen() o la accept() falliscono
 */
int waitForConnections ( int fd, int* cfd ) {
	
	if ( !cfd ) return errno = EINVAL;
	
	if ( listen( fd, SOMAXCONN ) == -1 )
		return errno;
	
	
	if ( ( *cfd = accept( fd, NULL, 0 ) ) == -1 )
		return errno;
	
	
	return 0;
}











/** @brief si connette con un server
 * 
 * @param sa la struttura con i dati del server
 * @param cfd dove verra' salvato il file descriptor della socket di comunicazione
 *
 * @return 0 se la connessione e' stata stabilita con successo
 * @return E* se la socket() o la connect() falliscono
 */
int connectToServer ( struct sockaddr_in* sa, int* cfd ) {
	
	
	if ( !sa || !cfd )
		return errno = EINVAL;
	
	if ( ( *cfd = socket( sa->sin_family, SOCK_STREAM, 0 ) ) == -1 )
		//fprintf( stderr, "%s: Error calling socket().\n", progName );
		return errno;
	
	
	//strncpy( sa.sun_path, SOCKET_NAME, UNIX_PATH_MAX );
	//sa.sun_family = AF_UNIX;
	if ( connect( *cfd, (void*) sa, sizeof( *sa ) ) == -1 )
		return errno;
	
	return 0;
}




