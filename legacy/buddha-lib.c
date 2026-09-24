

#include "buddha.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <argp.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include "../commons/various.h"
#include "../commons/png/cpng.h"


#define FREE_FCLOSE_RET( p, f, val ) \
	{ free( p ); fclose( f ); return val; }

#define FREE_RET( p, val ) { free( p ); return val; }



extern char* program_invocation_name;




void printState( buddha_t* b ) {
	printf( "Current state of the struct:\n" );
	
	printf( "  min, max: %lf %lf %lf %lf\n", b->minre, b->minim, b->maxre, b->maxim );
	printf( "  scale: %lf\n", b->scale );
	printf( "  data (low, high): %d %d\n", b->low, b->high );
	
	printf( "  points / iterations: %d %d\n", b->points, b->iteration );
	//printf( "seqsize: %d\n", b->seqsize );
	
	printf( "  out file: %s\n", b->image );
	printf( "  range: %lf %lf\n", b->rangeim, b->rangere );
	printf( "  size, w/h: %d %d %d\n", b->size, b->w, b->h );
	
	//printf( "  bench: %d\n", b->bench );
	printf( "  server: %s\n", b->server );
	
	printf( "  need loading: %d\n", b->load );
	printf( "  name: %s\n", b->name );
	printf( "  procs: %d\n", b->procs );
	printf( "  cmdLine: %s\n", b->cmd );
}






/// Salva tutti i token della linea di comando in una stringa di dimensione adatta
int getCommandLine( char** out, int argc, char** argv ) {
	int i, offset, size = 0;
	
	// guardo esattamente quanto e' lunga
	for ( i = 0; i < argc; size += strlen( argv[i] ), i++ );
	
	
	*out = malloc( size + argc );
	// scrivo nella stringa
	for( i = 0, offset = 0; i < argc; i++, offset++ )
		(*out)[offset += sprintf( *out + offset, "%s", argv[i] )] = ' ';
	
	// termino la stringa
	(*out)[offset - 1] = '\0';
	
	return size + argc;
}




/** @brief Estrae i token della linea di comando dalla stringa.
 * 
 * @param argc dove verra' salvato il numero di argomenti trovati.
 * @param argv un puntatore a un array da allocare dove salvare la roba.
 * @param cmd la stringa contenente la linea di comando. Verra' modificata.
 */
int getArguments( int* argc, char*** argv, char** cmd ) {
	int i = 0, size = 0;
	char* j;
	
	// conto gli spazi e mi alloco un array di stringhe grosso cosi'
	for ( j = *cmd; j; j = strchr( j+1, ' ' ), size++ );
	*argv = calloc( size, sizeof( char* ) );
	*argc = size;
	
	// salvo i token
	while ( *cmd && i < *argc )
		(*argv)[i++] = strsep( cmd, " " );
		
	return *argc = i;
}









static double getHistogramInfo ( pixel_t* arr, int size, pixel_t* min, pixel_t* max ) {
	double sum = 0;
	int i;
	
	for ( i = 0, *min = PIXEL_T_MAX, *max = 0; i < size; sum += arr[i++] ) {
		if ( arr[i] > *max ) *max = arr[i];
		if ( arr[i] < *min ) *min = arr[i];
	}
	
	return sum;
}








int buddha_eq ( buddha_t* a, buddha_t* b ) {
	
	int r = a->scale == b->scale && a->minre == b->minre &&
		a->minim == b->minim && a->maxre == b->maxre &&
		a->maxim == b->maxim && a->high == b->high;
		
	return r;
}






void clean ( int exitVal, buddha_t* b ) {
	
	free( b->buf );
	free( b->cmd );
}








// salva il file in un'area di memoria
int savem ( buddha_t* b, unsigned char** out, int* len ) {
	int l = strlen( b->cmd ) + 1;
	uLongf upper = compressBound( b->size );
	*out = calloc( upper + l + sizeof( int ), 1 );
	
	
	memcpy( *out, &l, sizeof( int ) );
	memcpy( *out + sizeof( int ), b->cmd, l );
	compress2( *out + l + sizeof( int ), &upper, (unsigned char*) b->buf, 
		   b->size, Z_DEFAULT_COMPRESSION );
	
	*len = upper + l + sizeof( int );
	
	return 0;
}









/**
 * @brief Salva i dati presenti in una struttura buddha_t.
 *
 */
int save ( buddha_t* b ) {
	int len; unsigned char* data;
	FILE* f;
	
	if ( !( f = fopen( b->name, "wb" ) ) )
		return errno = EIO;
	
	
	printf( "  Saving data in '%s'... ", b->name );
	savem( b, &data, &len );
	// salvo
	fwrite( data, 1, len, f );
	fclose( f );
	
	free( data );
	printf( "Data saved.\n" );
	
	return 0;
}




/// deve assicurarsi che i dati presenti nel file siano compatibili con quelli gia' 
/// presenti nella struttura (ricordo che arrivo qui dopo aver gia' parsato la linea di comando)
int load ( buddha_t* b ) {
	unsigned char* compressed;
	char** argv = NULL;
	int argc, size;
	char* cmd = NULL;
	buddha_t c;
	int len;
	FILE* f;
	
	
	
	if ( !( f = fopen( b->name, "rb" ) ) )
		return errno = ENOENT;
	
	// leggo la lunghezza della linea di comando
	fread( &len, 4, 1, f );
	cmd = malloc( len );
	// leggo la linea di comando
	fread( cmd, len, 1, f );
	
	
	// controllo che la linea di comando presente nel file sia compatibile
	getArguments( &argc, &argv, &cmd );
	if ( parseOptions( argc, argv, &c, ARGP_NO_EXIT ) )
		return errno = ENOMSG;
	
	if ( !buddha_eq( &c, b ) )
		return errno = EINVAL;
	
	
	
	
	// adesso posso leggere i dati compressi, inizio guardando quanta roba devo leggere
	fseek( f, 0, SEEK_END );
	size = ftell( f ) - 4 - len;
	fseek( f, 4 + len, SEEK_SET );
	
	// leggo dal file e chiudo
	compressed = malloc( size );
	fread( compressed, size, 1, f );
	fclose( f );
	
	// controllo per scrupolo che la dimensione decompressa combaci con quella attesa
	uLongf ssize = b->size;
	uncompress( (unsigned char*) b->buf, &ssize, compressed, size );
	if ( ssize != b->size )
		FREE_RET( compressed, errno = EIO );
	
	
	free( compressed );
	free( argv );
	free( cmd );
	
	printf( "  Loaded '%s'.\n", b->name );
	
	return 0;
}








/**
 * @brief Converte i buffer salvati nei vari canali in un'immagine png.
 * 
 * Questa funzione e' critica perche' bisogna stabilire quanto e quale significato dare ad ogni canale.
 * TODO la luminosita' adesso scala quasi perfettamente con il valore della scala.
 *      devo trovare un modo per farlo scalare bene anche con la dimensione della sequenza.
 */
int saveImage ( buddha_t* b ) {
	register double pixel;
	double mid, area, factor, contrast;
	unsigned int max, min;
	unsigned char* out;
	int x, y;
	
	printf( "  Saving preview in '%s'... ", b->image ); 
	fflush( stdout );
	
	#if SYMMETRICX 
	out = calloc ( b->size * 2, 1 );
	#else
	out = calloc ( b->size, 1 );
	#endif
	
	mid = getHistogramInfo( b->buf, b->w * b->h, &min, &max ) / ( b->w * b->h );
		printf( "%lf %d %d\n", mid, max, min );
	
	area = sqr( b->scale ) * b->rangeim * b->rangere;
	factor = sqrt( b->scale ) * 0.11; //deve dipendere anche dall'area! 0.22 per il buddha completo, 0.022 per testzoom
	//factor = 1.0;
	if ( b->mode == ANTIBUDDHA ) factor *= 0.5;
	
	for ( y = 0; y < b->h; y++ ) {
		for ( x = 0; x < b->w; x++ ) {
			#if 0 // ANTIBUDDHA
			//pixel = 0.35 * buf[c].multiplier * buf[c].data[i] / mid;
			pixel = 0.1 * log( b->sequenceSize * b->inf.scale ) * 
				pow ( b->buf[c].data[i] / (double) max, 0.3 );
			#endif
			
			// ottengo un valore tra 0 e 1
			pixel = ( b->buf[y * b->w + x] - min ) / (double) ( max - min );
			//pixel = pow(pixel, 0.7);
			
			
			if ( b->mode == ANTIBUDDHA ) pixel = pow( pixel, 0.3 ); // contrasto
			else pixel *= factor;
			
			//#if !SYMMETRICX
			//double boh = 10000.0 / max;
			//pixel = b->buf[c].data[y * b->w + x] / 255.0 * boh * b->scale * 0.0040;
			//#endif
			
			pixel = pixel > 1.0 ? 1.0 : pixel;
			pixel *= 255.0;
			
			out[b->w * y + x] = pixel;
			#if SYMMETRICX
			out[b->w * ( 2 * b->h - y - 1) + x] = pixel;
			#endif
		}
	}

	
	
	
	
	#if SYMMETRICX
	x = pngSave( b->image, out, b->w, b->h * 2, 8, 1, NULL );
	#else
	x = pngSave( b->image, out, b->w, b->h, 8, 1, NULL );
	#endif
	free( out );
	
	
	printf( "Preview saved.\n" );
	
	return x;
}




