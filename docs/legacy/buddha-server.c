
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <time.h>
#include <err.h>
#include <sys/types.h>
#include <signal.h>
#include "../commons/various.h"
#include "buddha.h"

/// TODO: il server riceve dati compressi dai client, o aggiunge un'opzione per farlo.
/// 1) prima legge i dati compressi
/// 2) poi in un secondo momento li aggiunge ai dati correnti
/// 
/// Ci possono essere varie soluzioni..
/// 1) leggo tutto poi prima di uscire decomprimo e aggiungo all'istogramma
/// 2) Un mutex per la socket, Un mutex per scrivere sull'istogramma. Ci sono vari thread decompressori.



volatile sig_atomic_t online = TRUE;
extern char *program_invocation_name;






void cleanServer ( int exitVal, void* bb ) {
	buddha_t* b = (buddha_t*) bb;
	
	clean( exitVal, b );
}






/** Funzione che gestisce l'arrivo di un segnale di terminazione
 */
void sigHandler ( int a ) {
	online = FALSE;
}




int main ( int argc, char** argv ) {
	struct sigaction s;
	int sfd, cfd;
	//int overwrite = TRUE;
	int received = 0;
	buddha_t b;
	
	bzero( &b, sizeof( buddha_t ) );
	bzero( &s, sizeof( struct sigaction ) );
	
	
	//on_exit( cleanServer, &b );
	
	s.sa_handler = sigHandler;
	if ( sigaction( SIGINT, &s, NULL ) == -1 ) {
		fprintf( stderr, "%s: Failed Inizialization of SIGINT handler\n", program_invocation_name );
		exit( EXIT_FAILURE );
	}
	
	
	printf( "  Waiting for connections of remote clients (^C to stop and save)...\n" );
	// set server parameters
	b.sa.sin_family = AF_INET;
	b.sa.sin_addr.s_addr = INADDR_ANY;
	b.sa.sin_port = htons( PORT );
	if ( createServerChannel( &(b.sa), &sfd ) )
		error( EXIT_FAILURE, errno, "createServerChannel()" );
	
	
	
	
	while ( online ) {
		if ( !waitForConnections( sfd, &cfd ) ) {
			printf( "    Receiving data from remote client...\n" );
			if ( !receiveData( &b, cfd, !received ) )
				printf( "    Received successfully.\n" ), received++;
			else	printf( "    Error receiving data.\n" );
		} //else 	error( 0, errno, "waitForConnections()" );
	}
	
	//printState( &b );
	//getchar();
	printf( "  Received data from %d clients.\n", received );
	
	
	if ( received ) {
		//save( &b );
		saveImage( &b );
	}




	exit( EXIT_SUCCESS );
}

