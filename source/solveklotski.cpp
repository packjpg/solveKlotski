#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puzzles.h"

#define CRC32_SEED 0xFFFFFFFF // crc32 start value
#define CRC32_POLY 0xEDB88320 // crc32 polynomial
#define CRC32_CALC(crc,byte) ( ( ( crc >> 8 ) & 0x00FFFFFF ) ^ ( crc32_table[ ( crc ^ (byte) ) & 0xFF ] ) )

#define MAX_DEPTH 500
#define IC 40


struct field_and_crc {
	unsigned char* field;
	unsigned int crc32;
	field_and_crc* next;
};

struct tiledesc {
	unsigned char* form; // form
	unsigned char* dform[4]; // diff form
	int id; // unique id
	int type; // tile type 
	int w; // width
	int h; // height
	int gx; // goal x
	int gy; // goal y
	int gp; // goal p
};

struct tiledef {
	tiledesc* desc; // tile desc
	int x; // x position
	int y; // y position
	int p; // 1D position
	tiledef* pos[4]; // different positions
};

struct node {
	unsigned char* field; // type field scheme
	tiledef** tiles; // all tile positions
	node* mother; // mother node
	node* next; // next sibling node
};

int ntt; // # of tiles total
int ntg; // # of goal tiles



void err_exit( const char* msg )
{
	fprintf( stderr, "\nerror: %s\n\n", msg );
	exit( -1 );
}


unsigned int* build_crc32_table( void ) {
	static unsigned int* crc32_table = (unsigned int*) calloc( 256, sizeof( int ) );
	unsigned int crc;
	int i;
	
	
	// fill crc table
	for ( i = 0; i < 256; i++ ) {
		crc = i;
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc = ( crc & 1 ) ? ( ( crc >> 1 ) ^ CRC32_POLY ) : ( crc >> 1 );
		crc32_table[ i ] = crc;
	}
	
	
	return crc32_table;
}


node* build_node( void )
{
	node* nnode;
	
	
	// alloc memory, check
	nnode = (node*) calloc( 1, sizeof( node ) );
	if ( nnode == NULL ) err_exit( "out of memory" );
	nnode->field = (unsigned char*) calloc( H*W, sizeof( char ) );
	if ( nnode->field == NULL ) err_exit( "out of memory" );
	nnode->tiles = (tiledef**) calloc( ntt, sizeof( tiledef* ) );
	if ( nnode->tiles == NULL ) err_exit( "out of memory" );
	nnode->next = NULL;
	
	
	return nnode;
}


void copy_node( node* cnode, node* mnode )
{
	// copy mother data to child
	memcpy( cnode->field, mnode->field, H*W * sizeof( char ) );
	memcpy( cnode->tiles, mnode->tiles, ntt * sizeof( tiledef* ) );
	cnode->mother = mnode;
	
	
	return;
}


bool check_move( unsigned char* field, tiledef* tile, int d )
{
	tiledef* ntile = tile->pos[d];
	unsigned char* dform;
	int p0, p;
	
	
	// check if tile can be moved
	if ( ntile == NULL ) return false;
	p0 = ntile->p; dform = ntile->desc->dform[d];
	for ( p = 0; dform[p] != 0xFF; p++ ) if ( dform[p] == 1 )
		if ( field[p+p0] != 0 ) return false;
		
	
	return true;
}


tiledef* do_move( unsigned char* field, tiledef* tile, int d )
{
	tiledef* ntile = tile->pos[d];
	int tp = tile->desc->type;
	unsigned char* dform;
	int p0, p;
	
	
	// remove old tile
	p0 = tile->p; dform = tile->desc->dform[(d+2)%4];
	for ( p = 0; dform[p] != 0xFF; p++ ) if ( dform[p] == 1 )
		field[p+p0] = 0;
		
	// insert new tile
	p0 = ntile->p; dform = ntile->desc->dform[d];
	for ( p = 0; dform[p] != 0xFF; p++ ) if ( dform[p] == 1 )
		field[p+p0] = tp;
		
	
	return ntile;
}


bool check_node( node* cnode )
{
	static unsigned int* crc32_table = build_crc32_table();
	// static field_and_crc** fields = (field_and_crc**) calloc( 1 << 16, sizeof( field_and_crc* ) );
	static field_and_crc* fields[1<<16] = { NULL };
	unsigned char* nfield = cnode->field;
	field_and_crc* cfield;
	unsigned int crc32;
	int idx;
	int i;
	
	
	// calculate crc32
	for ( i = 0, crc32 = CRC32_SEED; i < H*W; i++ )
		crc32 = CRC32_CALC( crc32, nfield[i] );
	// index = first 16 crc32 bits
	idx = crc32 & 0xFFFF;
	
	// compare known states with new
	for ( cfield = fields[idx]; cfield != NULL; cfield = cfield->next )
		if ( crc32 == cfield->crc32 ) if ( memcmp( nfield, cfield->field, H*W*sizeof(char) ) == 0 ) break;
		
	// previously unknown state found
	if ( cfield == NULL ) {
		cfield = (field_and_crc*) calloc( 1, sizeof( field_and_crc ) );
		if ( cfield == NULL ) err_exit( "out of memory" );
		cfield->field = nfield;
		cfield->crc32 = crc32;
		cfield->next = fields[idx];
		fields[idx] = cfield;
	} else return false;
	
	
	return true;
}


bool check_goal_condition( node* cnode )
{
	int i;
	
	
	for ( i = 0; i < ntg; i++ )
		if ( cnode->tiles[i]->p != cnode->tiles[i]->desc->gp ) break;
	if ( i == ntg ) return true;
	
	
	return false;
}


node* convert_puzzle( void )
{
	unsigned char* field;
	tiledef* tgrid[H][W];
	tiledef** tiles;
	tiledef* swap;
	node* node0;
	int ntp;
	int x, y;
	int p, d;
	int i;
	
	
	// alloc memory
	node0 = (node*) calloc( 1, sizeof( node ) );
	field = (unsigned char*) calloc( H*W, sizeof( char ) );
	tiles = (tiledef**) calloc( 1, sizeof( tiledef* ) );
	
	// preset known node variables
	node0->field = field;
	node0->mother = NULL;
	node0->next = NULL;
	
	// seek and catalogue puzzle tiles, build base field form
	for ( x = 0, ntt = 0; x < W; x++ ) for ( y = 0; y < H; y++ ) {
		if ( puzzle[y][x] == '#' ) // solid tiles
			field[y*W+x] = 0xFF;
		else if ( ( puzzle[y][x] != '.' ) && ( puzzle[y][x] != ' ' ) ) {
			for ( i = 0; i < ntt; i++ ) // found one, check if new
				if ( tiles[i]->desc->id == puzzle[y][x] ) break;
			if ( i == ntt ) { // previously unknown tile
				ntt++; // increment tile count
				if ( ntt > 1 ) tiles = (tiledef**) realloc( tiles, ntt * sizeof( tiledef* ) );
				tiles[ntt-1] = (tiledef*) calloc( 1, sizeof( tiledef ) );
				tiles[ntt-1]->desc = (tiledesc*) calloc( 1, sizeof( tiledesc ) );
				tiles[ntt-1]->desc->form = (unsigned char*) calloc( H*W+1, sizeof( char ) );
				for ( d = 0; d < 4; d++ ) tiles[ntt-1]->desc->dform[d] =
					(unsigned char*) calloc( H*W+1, sizeof( char ) );
				tiles[ntt-1]->desc->id = puzzle[y][x];
				tiles[ntt-1]->desc->gx = -1; // check this later
				tiles[ntt-1]->desc->gy = -1; // check this later
				tiles[ntt-1]->x = -1; // find this later
				tiles[ntt-1]->y = -1; // find this later
			}
		}
	}
	
	// count and move goal tiles to front
	for ( x = 0, ntg = 0; x < W; x++ ) for ( y = 0; y < H; y++ ) {
		if ( solve[y][x] == '#' ) {
			if ( puzzle[y][x] != '#' ) err_exit( "solid tiles don't match" );
		} else if ( ( solve[y][x] != '.' ) && ( solve[y][x] != ' ' ) ) {
			for ( i = ntt - 1; i >= 0; i-- ) // seek id in known tiles
				if ( tiles[i]->desc->id == solve[y][x] ) break;
			if ( i == -1 ) err_exit( "goal tile not present in puzzle" );
			else if ( i >= ntg ) { // bubble sort unmoved tiles to front
				for ( ; i > ntg; i-- ) {
					swap = tiles[i];
					tiles[i] = tiles[i-1];
					tiles[i-1] = swap;
				}
				ntg++; // increment goal tile counter
			}
		}
	}
	
	// analyze tiles
	for ( i = 0, ntp = 0; i < ntt; i++ ) {
		// -- x and y position --
		for ( x = 0; x < W; x++ ) {
			for ( y = 0; y < H; y++ ) if ( puzzle[y][x] == tiles[i]->desc->id ) break;
			if ( y < H ) { // x position
				tiles[i]->x = x;
				break;
			}
		}
		for ( y = 0; y < H; y++ ) {
			for ( x = 0; x < W; x++ ) if ( puzzle[y][x] == tiles[i]->desc->id ) break;
			if ( x < W ) { // y position
				tiles[i]->y = y;
				break;
			}
		}		
		// -- width and height --
		for ( x = W-1; x >= 0; x-- ) {
			for ( y = 0; y < H; y++ ) if ( puzzle[y][x] == tiles[i]->desc->id ) break;
			if ( y < H ) { // width
				tiles[i]->desc->w = x - tiles[i]->x + 1;
				break;
			}
		}
		for ( y = H-1; y >= 0; y-- ) {
			for ( x = 0; x < W; x++ ) if ( puzzle[y][x] == tiles[i]->desc->id ) break;
			if ( x < W ) { // height
				tiles[i]->desc->h = y - tiles[i]->y + 1;
				break;
			}
		}
		// -- tile form ---
		for ( y = tiles[i]->y, x = tiles[i]->x, p = 0; y < H; y++, x = 0 ) for ( ; x < W; x++, p++ )
			if ( puzzle[y][x] == tiles[i]->desc->id ) tiles[i]->desc->form[p] = 1; // form
		for ( p = H*W - 1; p >= 0; p-- ) if ( tiles[i]->desc->form[p] == 1 ) break;
		memset( tiles[i]->desc->form+p+1, 0xFF, H*W-p );
		// -- tile types --
		if ( i < ntg ) tiles[i]->desc->type = ++ntp; // goal tiles: always unique
		else { // other tiles: compare with previous forms 
			for ( p = i-1; p >= ntg; p-- )
				if ( memcmp( tiles[i]->desc->form, tiles[p]->desc->form, H*W*sizeof( char ) ) == 0 ) break;
			if ( p < ntg ) tiles[i]->desc->type = ++ntp; // new unique type
			else tiles[i]->desc->type = tiles[p]->desc->type; // known type
		}
		// -- tile diff forms --
		if ( tiles[i]->desc->h < H ) { // up/down
			for ( y = 0, p = 0; y < H; y++ ) for ( x = 0; x < W; x++, p++ ) {
				if ( y == 0 ) tiles[i]->desc->dform[0][p] = tiles[i]->desc->form[p];
				else if ( ( tiles[i]->desc->form[p] == 1 ) && ( tiles[i]->desc->form[p-W] != 1 ) )
					tiles[i]->desc->dform[0][p] = 1; // up
				if ( y == H-1 ) tiles[i]->desc->dform[2][p] = tiles[i]->desc->form[p];
				else if ( ( tiles[i]->desc->form[p] == 1 ) && ( tiles[i]->desc->form[p+W] != 1 ) )
					tiles[i]->desc->dform[2][p] = 1; // down
			}
		}
		if ( tiles[i]->desc->w < W ) { // left/right
			for ( y = 0, p = 0; y < H; y++ ) for ( x = 0; x < W; x++, p++ ) {
				if ( x == 0 ) tiles[i]->desc->dform[1][p] = tiles[i]->desc->form[p];
				else if ( ( tiles[i]->desc->form[p] == 1 ) && ( tiles[i]->desc->form[p-1] != 1 ) )
					tiles[i]->desc->dform[1][p] = 1; // left
				if ( x == W-1 ) tiles[i]->desc->dform[3][p] = tiles[i]->desc->form[p];
				else if ( ( tiles[i]->desc->form[p] == 1 ) && ( tiles[i]->desc->form[p+1] != 1 ) )
					tiles[i]->desc->dform[3][p] = 1; // right
			}
		}
		for ( d = 0; d < 4; d++ ) {
			for ( p = H*W-1; p >= 0; p-- ) if ( tiles[i]->desc->dform[d][p] == 1 ) break;
			memset( tiles[i]->desc->dform[d]+p+1, 0xFF, H*W-p );
		}
		/*fprintf( stderr, "\n\nt%i: base",i );
		for ( p = 0; p < H*W; p++ ) {
			if ( p % W == 0 ) fprintf( stderr, "\n" );
			fprintf( stderr, "%02X ", tiles[i]->desc->form[p] );
		}
		for ( d = 0; d < 4; d++ ){
			fprintf( stderr, "\n\nt%i: %s", i, (d==0)?"up":(d==1)?"left":(d==2)?"down":(d==3)?"right":"" );
			for ( p = 0; p < H*W; p++ ) {
				if ( p % W == 0 ) fprintf( stderr, "\n" );
				fprintf( stderr, "%02X ", tiles[i]->desc->dform[d][p] );
			}
		}*/
	}
	
	// additional info and safety checks for goal tiles
	if ( ntg == 0 ) err_exit( "no goal tiles found, nothing to do" );
	for ( i = 0; i < ntg; i++ ) {
		// --- find goal x and y position ---
		for ( x = 0; x < W; x++ ) {
			for ( y = 0; y < H; y++ ) if ( solve[y][x] == tiles[i]->desc->id ) break;
			if ( y < H ) { // goal x position
				tiles[i]->desc->gx = x;
				break;
			}
		}
		for ( y = 0; y < H; y++ ) {
			for ( x = 0; x < W; x++ ) if ( solve[y][x] == tiles[i]->desc->id ) break;
			if ( x < W ) { // goal y position
				tiles[i]->desc->gy = y;
				break;
			}
		}
		tiles[i]->desc->gp = tiles[i]->desc->gy * W + tiles[i]->desc->gx;
		// --- compare form ---
		for ( y = tiles[i]->desc->gy, x = tiles[i]->desc->gx, p = 0; y < H; y++, x = 0 ) for ( ; x < W; x++, p++ ) {
			if ( solve[y][x] == tiles[i]->desc->id ) {
				if ( tiles[i]->desc->form[p] == 0 ) err_exit( "goal tile forms don't match" );
			} else if ( tiles[i]->desc->form[p] == 1 ) err_exit( "goal tile forms don't match" );
		}
		for ( ; p < H*W; p++ ) if ( tiles[i]->desc->form[p] == 1 )
			err_exit( "goal tile forms don't match" );
	}
	
	// build all possible tiledef positions
	for ( i = 0; i < ntt; i++ ) {
		for ( y = 0; y <= H - tiles[i]->desc->h; y++ ) for ( x = 0; x <= W - tiles[i]->desc->w; x++ ) {
			if ( ( x != tiles[i]->x ) || ( y != tiles[i]->y ) ) {
				tgrid[y][x] = (tiledef*) calloc( 1, sizeof( tiledef ) );
				tgrid[y][x]->desc = tiles[i]->desc;
				tgrid[y][x]->x = x;
				tgrid[y][x]->y = y;
			} else tgrid[y][x] = tiles[i];
			// add 1D position
			tgrid[y][x]->p = y*W + x;
			// default: no connections
			tgrid[y][x]->pos[0] = NULL;
			tgrid[y][x]->pos[1] = NULL;
			tgrid[y][x]->pos[2] = NULL;
			tgrid[y][x]->pos[3] = NULL;
			// build connections
			if ( y > 0 ) { // vertical (up/down)
				tgrid[y][x]->pos[0] = tgrid[y-1][x];
				tgrid[y-1][x]->pos[2] = tgrid[y][x];
			}
			if ( x > 0 ) { // horizontal (left/right)
				tgrid[y][x]->pos[1] = tgrid[y][x-1];
				tgrid[y][x-1]->pos[3] = tgrid[y][x];
			}
		}
	}
	
	// insert tiles
	node0->tiles = tiles;
	for ( i = 0; i < ntt; i++ ) {
		for ( p = 0; tiles[i]->desc->form[p] != 0xFF; p++ ) if ( tiles[i]->desc->form[p] == 1 ) {
			if ( field[p+tiles[i]->p] != 0 ) err_exit( "something went terribly wrong" );
			else field[p+tiles[i]->p] = tiles[i]->desc->type;
		}
	}
	if ( !check_node( node0 ) ) err_exit( "something went terribly wrong" );
	
	
	// all done, ready to go!
	return node0;
}


void display_puzzle_and_goal( void )
{
	int x, y;
	
	// display puzzle/goal on screen
	fprintf( stderr, "puzzle -> goal:\n" );
	fprintf( stderr, "  " );
	for ( x = 0; x < W; x++ ) fprintf( stderr, "-" );
	fprintf( stderr, "   " );
	for ( x = 0; x < W; x++ ) fprintf( stderr, "-" );
	fprintf( stderr, " " );
	fprintf( stderr, "\n" );
	for ( y = 0; y < H; y++ ) {
		fprintf( stderr, " |" );
		for ( x = 0; x < W; x++ ) fprintf( stderr, "%c", puzzle[y][x] );
		fprintf( stderr, "| |" );
		for ( x = 0; x < W; x++ ) fprintf( stderr, "%c", solve[y][x] );
		fprintf( stderr, "| " );
		fprintf( stderr, "\n" );
	}
	fprintf( stderr, "  " );
	for ( x = 0; x < W; x++ ) fprintf( stderr, "-" );
	fprintf( stderr, "   " );
	for ( x = 0; x < W; x++ ) fprintf( stderr, "-" );
	fprintf( stderr, "   " );
	fprintf( stderr, "\n\n" );
	
	return;
}


void output_image( node* cnode, int s ) {
	unsigned char image[IC*IC*H*W] = { 0 };
	unsigned char grid[H][W];
	unsigned char c;
	tiledef* tile;
	FILE* fp;
	char fn[256];
	int px, py;
	int x, y;
	int p, i;
	
	
	// build grid
	memset( grid, 0x00, H*W*sizeof(char) );
	for ( y = 0, p = 0; y < H; y++ ) for ( x = 0; x < W; x++, p++ )
		grid[y][x] = cnode->field[p];
	for ( i = 0; i < ntt; i++ ) {
		tile = cnode->tiles[i];
		for ( y = tile->y, x = tile->x, p = 0; y < H; y++, x = 0 ) for ( ; x < W; x++, p++ )
			if ( tile->desc->form[p] == 1 ) grid[y][x] = i+1;
	}
	
	// translate grid to image
	for ( py = 0; py < H; py++ ) for ( px = 0; px < W; px++ ) {
		/*c = ( grid[py][px] == 0x00 ) ? 0x00 :
			( grid[py][px] == 0xFF ) ? 0x40 :
			( grid[py][px] <=  ntg ) ? 0xC0 : 0x80;*/
		c = ( grid[py][px] == 0x00 ) ? 0x20 :
			( grid[py][px] == 0xFF ) ? 0x00 :
			( grid[py][px] <=  ntg ) ? 0xC0 : 0x80;
		// paint inner square
		for ( y = py * IC + 1; y < (py+1) * IC - 1; y++ ) 
			for ( x = px * IC + 1; x < (px+1) * IC - 1; x++ )
				image[ y * W * IC + x ] = c;
		// paint connections
		if ( py > 0 ) if ( grid[py][px] == grid[py-1][px] ) // top
			for ( x = px * IC + 1, y = py * IC; x < (px+1) * IC - 1; x++ )
				image[ y * W * IC + x ] = c;
		if ( px > 0 ) if ( grid[py][px] == grid[py][px-1] ) // left
			for ( y = py * IC + 1, x = px * IC; y < (py+1) * IC - 1; y++ )
				image[ y * W * IC + x ] = c;
		if ( py < H-1 ) if ( grid[py][px] == grid[py+1][px] ) // bottom
			for ( x = px * IC + 1, y = (py+1) * IC - 1; x < (px+1) * IC - 1; x++ )
				image[ y * W * IC + x ] = c;
		if ( px < W-1 ) if ( grid[py][px] == grid[py][px+1] ) // right
			for ( y = py * IC + 1, x = (px+1) * IC - 1; y < (py+1) * IC - 1; y++ )
				image[ y * W * IC + x ] = c;
		// inner connections
		if ( ( py < H-1 ) && ( px < W-1 ) ) {
			if ( ( grid[py][px] == grid[py+1][px] ) &&
				 ( grid[py][px] == grid[py][px+1] ) &&
				 ( grid[py][px] == grid[py+1][px+1] ) ) {
				image[ ((py+1)*IC-1)*W*IC + ((px+1)*IC-1) ] = c;
				image[ ((py+1)*IC-1)*W*IC + ((px+1)*IC-0) ] = c;
				image[ ((py+1)*IC-0)*W*IC + ((px+1)*IC-1) ] = c;
				image[ ((py+1)*IC-0)*W*IC + ((px+1)*IC-0) ] = c;
			}
		}
	}
	
	// image done, write to file
	sprintf( fn, "bmove_solution_%03i.pgm", s );
	fp = fopen( fn, "wb" );
	if ( fp == NULL ) err_exit( "couldn't write image" );
	fprintf( fp, "P5\n%i %i\n255\n", W * IC, H * IC );
	fwrite( image, sizeof( char ), IC*IC*W*H, fp );
	fclose( fp );
	
	
	return;
}


int main(int argc, char** argv)
{
	tiledef* ntile;
	node* nodes[MAX_DEPTH+1];
	node* mnode;
	node* cnode;
	node* snode;
	node* fnode;
	int nn;
	int s, t, d;

	
	// headline
	fprintf( stderr, "\n%s\n", "--- block sliding puzzle solver v0.4 by Matthias Stirner ---\n" );
	
	// display summary
	display_puzzle_and_goal();
	
	// convert puzzle
	nodes[0] = convert_puzzle();
	
	// debug info
	/* for ( int p = 0; p < H*W; p++ ) {
		if ( p % W == 0 ) fprintf( stderr, "\n" );
		fprintf( stderr, "%02X ", nodes[0]->field[p] );
	}
	for ( int i = 0; i < ntt; i++ ) 
		fprintf( stderr, "id%c, type%02X, (%i/%i)->(%i/%i), (%ix%i) [%i]\n",
			nodes[0]->tiles[i]->desc->id, nodes[0]->tiles[i]->desc->type,
			nodes[0]->tiles[i]->x, nodes[0]->tiles[i]->y,
			nodes[0]->tiles[i]->desc->gx, nodes[0]->tiles[i]->desc->gy,
			nodes[0]->tiles[i]->desc->w, nodes[0]->tiles[i]->desc->h,
			nodes[0]->tiles[i]->desc->gp );*/
	
	// start finding solutions
	for ( s = 1, nn = 1,fnode = NULL, cnode = build_node(); s <= MAX_DEPTH; s++ ) {		
		for ( mnode = nodes[s-1], snode = NULL; mnode != NULL; mnode = mnode->next ) {
			copy_node( cnode, mnode ); // copy mother to child
			// cnode = child_node( mnode ); // create new child node
			for ( t = 0; (t < ntt) && (fnode == NULL); t++ ) { // tile counter
				for ( d = 0; (d < 4) && (fnode == NULL); d++ ) { // try move tile t in direction d
					if ( !check_move( cnode->field, cnode->tiles[t], d ) ) continue; // check move
					ntile = do_move( cnode->field, cnode->tiles[t], d ); // move tile
					if ( check_node( cnode ) ) { // check if new
						cnode->tiles[t] = ntile;
						if ( snode == NULL ) nodes[s] = cnode;
						else snode->next = cnode;
						snode = cnode;
						cnode = build_node();
						copy_node( cnode, mnode );
						// cnode = child_node( mnode );
						// announce progress
						fprintf( stderr, "current step: %i / states found: %i\r", s, ++nn );
						// goal conditions met?
						if ( check_goal_condition( snode ) ) {
							fnode = snode;
							break;
						}
					} else do_move( cnode->field, ntile, (d+2)%4 ); // move tile back
				}
			} if ( fnode != NULL ) break;
		} if ( fnode != NULL ) break;
	}
	
	
	// summary
	if ( fnode == NULL ) fprintf( stderr, "\n->finished, no solutions found!\n" );
	else {
		fprintf( stderr, "\n-> finished, best solution has %i steps\n", s );
		for ( int i = s; fnode != NULL; fnode = fnode->mother, i-- ) {
			fprintf( stderr, "-> dumping solution: %i of %i\r", s-i, s );
			output_image( fnode, i ); 
		}
		fprintf( stderr, "\n\n" );
	}
	
	
	return 0;
}
