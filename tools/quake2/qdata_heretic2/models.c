/*
   Copyright (C) 1999-2006 Id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include "qdata.h"
#include <assert.h>
#include "jointed.h"
#include "fmodel.h"

//=================================================================

typedef struct
{
	int numnormals;
	vec3_t normalsum;
} vertexnormals_t;

typedef struct
{
	vec3_t v;
	int lightnormalindex;
} trivert_t;

typedef struct
{
	vec3_t mins, maxs;
	char name[16];
	trivert_t v[MAX_VERTS];
	QDataJoint_t joints[NUM_CLUSTERS];    // ,this
} frame_t;

// ,and all of this should get out of here, need to use new stuff in fmodels instead

typedef struct IntListNode_s
{
	int data;
	struct IntListNode_s *next;
} IntListNode_t;  // gaak

typedef struct
{
	float scale[3];         // multiply byte verts by this
	float translate[3];         // then add this
} PartialAliasFrame_t;

int jointed;
int clustered;

int *clusters[NUM_CLUSTERS];
IntListNode_t *vertLists[NUM_CLUSTERS];
int num_verts[NUM_CLUSTERS + 1];
int new_num_verts[NUM_CLUSTERS + 1];

// end that

//================================================================

frame_t g_frames[MAX_FRAMES];
//frame_t		*g_frames;

static dmdl_t model;


float scale_up;                 // set by $scale
vec3_t adjust;                  // set by $origin
int g_fixedwidth, g_fixedheight;            // set by $skinsize


//
// base frame info
//
dstvert_t base_st[MAX_VERTS];
dtriangle_t triangles[MAX_TRIANGLES];

static int triangle_st[MAX_TRIANGLES][3][2];

// the command list holds counts, s/t values, and xyz indexes
// that are valid for every frame
int commands[16384];
int numcommands;
int numglverts;
int used[MAX_TRIANGLES];

char g_skins[MAX_MD2SKINS][64];

char cdarchive[1024];
char cdpartial[1024];
char cddir[1024];

char modelname[64];         // empty unless $modelname issued (players)

extern char        *g_outputDir;

#define NUMVERTEXNORMALS    162

float avertexnormals[NUMVERTEXNORMALS][3] =
{
	#include "anorms.h"
};

unsigned char pic[SKINPAGE_HEIGHT * SKINPAGE_WIDTH], pic_palette[768];

FILE    *headerouthandle = NULL;

//==============================================================

/*
   ===============
   ClearModel
   ===============
 */
static void ClearModel( void ){
	memset( &model, 0, sizeof( model ) );

	modelname[0] = 0;
	jointed = NOT_JOINTED;
	clustered = 0;
	scale_up = 1.0;
	VectorCopy( vec3_origin, adjust );
	g_fixedwidth = g_fixedheight = 0;
	g_skipmodel = false;
}


void H_printf( char *fmt, ... ){
	va_list argptr;
	char name[1024];

	if ( !headerouthandle ) {
		sprintf( name, "%s/tris.h", cddir );
		headerouthandle = SafeOpenWrite( name );
		fprintf( headerouthandle, "// %s\n\n", cddir );
		fprintf( headerouthandle, "// This file generated by qdata - Do NOT Modify\n\n" );
	}

	va_start( argptr, fmt );
	vfprintf( headerouthandle, fmt, argptr );
	va_end( argptr );
}

/*
   ============
   WriteModelFile
   ============
 */
void WriteCommonModelFile( FILE *modelouthandle, PartialAliasFrame_t *outFrames ){
	int i;
	dmdl_t modeltemp;
	int j, k;
	frame_t         *in;
	daliasframe_t   *out;
	byte buffer[MAX_VERTS * 4 + 128];
	float v;
	int c_on, c_off;

	model.version = ALIAS_VERSION;
	model.framesize = (int)&( (daliasframe_t *)0 )->verts[model.num_xyz];
	model.num_glcmds = numcommands;
	model.ofs_skins = sizeof( dmdl_t );
	model.ofs_st = model.ofs_skins + model.num_skins * MAX_SKINNAME;
	model.ofs_tris = model.ofs_st + model.num_st * sizeof( dstvert_t );
	model.ofs_frames = model.ofs_tris + model.num_tris * sizeof( dtriangle_t );
	model.ofs_glcmds = model.ofs_frames + model.num_frames * model.framesize;
	model.ofs_end = model.ofs_glcmds + model.num_glcmds * sizeof( int );
	//
	// write out the model header
	//
	for ( i = 0 ; i < sizeof( dmdl_t ) / 4 ; i++ )
		( (int *)&modeltemp )[i] = LittleLong( ( (int *)&model )[i] );

	SafeWrite( modelouthandle, &modeltemp, sizeof( modeltemp ) );

	//
	// write out the skin names
	//
	SafeWrite( modelouthandle, g_skins, model.num_skins * MAX_SKINNAME );

	//
	// write out the texture coordinates
	//
	c_on = c_off = 0;
	for ( i = 0 ; i < model.num_st ; i++ )
	{
		base_st[i].s = LittleShort( base_st[i].s );
		base_st[i].t = LittleShort( base_st[i].t );
	}

	SafeWrite( modelouthandle, base_st, model.num_st * sizeof( base_st[0] ) );

	//
	// write out the triangles
	//
	for ( i = 0 ; i < model.num_tris ; i++ )
	{
		int j;
		dtriangle_t tri;

		for ( j = 0 ; j < 3 ; j++ )
		{
			tri.index_xyz[j] = LittleShort( triangles[i].index_xyz[j] );
			tri.index_st[j] = LittleShort( triangles[i].index_st[j] );
		}

		SafeWrite( modelouthandle, &tri, sizeof( tri ) );
	}

	//
	// write out the frames
	//
	for ( i = 0 ; i < model.num_frames ; i++ )
	{
		in = &g_frames[i];
		out = (daliasframe_t *)buffer;

		strcpy( out->name, in->name );
		for ( j = 0 ; j < 3 ; j++ )
		{
			out->scale[j] = ( in->maxs[j] - in->mins[j] ) / 255;
			out->translate[j] = in->mins[j];

			if ( outFrames ) {
				outFrames[i].scale[j] = out->scale[j];
				outFrames[i].translate[j] = out->translate[j];
			}
		}

		for ( j = 0 ; j < model.num_xyz ; j++ )
		{
			// all of these are byte values, so no need to deal with endianness
			out->verts[j].lightnormalindex = in->v[j].lightnormalindex;

			for ( k = 0 ; k < 3 ; k++ )
			{
				// scale to byte values & min/max check
				v = Q_rint( ( in->v[j].v[k] - out->translate[k] ) / out->scale[k] );

				// clamp, so rounding doesn't wrap from 255.6 to 0
				if ( v > 255.0 ) {
					v = 255.0;
				}
				if ( v < 0 ) {
					v = 0;
				}
				out->verts[j].v[k] = v;
			}
		}

		for ( j = 0 ; j < 3 ; j++ )
		{
			out->scale[j] = LittleFloat( out->scale[j] );
			out->translate[j] = LittleFloat( out->translate[j] );
		}

		SafeWrite( modelouthandle, out, model.framesize );
	}

	//
	// write out glcmds
	//
	SafeWrite( modelouthandle, commands, numcommands * 4 );
}

/*
   ============
   WriteModelFile
   ============
 */
void WriteModelFile( FILE *modelouthandle ){
	model.ident = IDALIASHEADER;

	WriteCommonModelFile( modelouthandle, NULL );
}

/*
   ============
   WriteJointedModelFile
   ============
 */
void WriteJointedModelFile( FILE *modelouthandle ){
	int i;
	int j, k;
	frame_t         *in;
	float v;
	IntListNode_t   *current, *toFree;
	PartialAliasFrame_t outFrames[MAX_FRAMES];

	model.ident = IDJOINTEDALIASHEADER;

	WriteCommonModelFile( modelouthandle, outFrames );

	// Skeletal Type
	SafeWrite( modelouthandle, &jointed, sizeof( int ) );

	// number of joints
	SafeWrite( modelouthandle, &numJointsForSkeleton[jointed], sizeof( int ) );

	// number of verts in each cluster
	SafeWrite( modelouthandle, &new_num_verts[1], sizeof( int ) * numJointsForSkeleton[jointed] );

	// cluster verts
	for ( i = 0; i < new_num_verts[0]; ++i )
	{
		current = vertLists[i];
		while ( current )
		{
			SafeWrite( modelouthandle, &current->data, sizeof( int ) );
			toFree = current;
			current = current->next;
			free( toFree );  // freeing of memory allocated in ReplaceClusterIndex called in Cmd_Base
		}
	}

	for ( i = 0 ; i < model.num_frames ; i++ )
	{
		in = &g_frames[i];

		for ( j = 0 ; j < new_num_verts[0]; ++j )
		{
			for ( k = 0 ; k < 3 ; k++ )
			{
				// scale to byte values & min/max check
				v = Q_rint( ( in->joints[j].placement.origin[k] - outFrames[i].translate[k] ) / outFrames[i].scale[k] );

				// clamp, so rounding doesn't wrap from 255.6 to 0
				if ( v > 255.0 ) {
					v = 255.0;
				}

				if ( v < 0 ) {
					v = 0;
				}

				// write out origin as a float (there's only a few per model, so it's not really
				// a size issue)
				SafeWrite( modelouthandle, &v, sizeof( float ) );
			}

			for ( k = 0 ; k < 3 ; k++ )
			{
				v = Q_rint( ( in->joints[j].placement.direction[k] - outFrames[i].translate[k] ) / outFrames[i].scale[k] );

				// clamp, so rounding doesn't wrap from 255.6 to 0
				if ( v > 255.0 ) {
					v = 255.0;
				}

				if ( v < 0 ) {
					v = 0;
				}

				// write out origin as a float (there's only a few per model, so it's not really
				// a size issue)
				SafeWrite( modelouthandle, &v, sizeof( float ) );
			}

			for ( k = 0 ; k < 3 ; k++ )
			{
				v = Q_rint( ( in->joints[j].placement.up[k] - outFrames[i].translate[k] ) / outFrames[i].scale[k] );

				// clamp, so rounding doesn't wrap from 255.6 to 0
				if ( v > 255.0 ) {
					v = 255.0;
				}

				if ( v < 0 ) {
					v = 0;
				}

				// write out origin as a float (there's only a few per model, so it's not really
				// a size issue)
				SafeWrite( modelouthandle, &v, sizeof( float ) );
			}
		}
	}
}

/*
   ===============
   FinishModel
   ===============
 */
void FinishModel( void ){
	FILE        *modelouthandle;
	int i;
	char name[1024];

	if ( !model.num_frames ) {
		return;
	}

//
// copy to release directory tree if doing a release build
//
	if ( g_release ) {
		if ( modelname[0] ) {
			sprintf( name, "%s", modelname );
		}
		else{
			sprintf( name, "%s/tris.md2", cdpartial );
		}
		ReleaseFile( name );

		for ( i = 0 ; i < model.num_skins ; i++ )
		{
			ReleaseFile( g_skins[i] );
		}
		model.num_frames = 0;
		return;
	}

//
// write the model output file
//
	if ( modelname[0] ) {
		sprintf( name, "%s%s", g_outputDir, modelname );
	}
	else{
		sprintf( name, "%s/tris.md2", g_outputDir );
	}
	printf( "saving to %s\n", name );
	CreatePath( name );
	modelouthandle = SafeOpenWrite( name );

	if ( jointed != NOT_JOINTED ) {
		WriteJointedModelFile( modelouthandle );
	}
	else
	WriteModelFile( modelouthandle );

	printf( "%3dx%3d skin\n", model.skinwidth, model.skinheight );
	printf( "First frame boundaries:\n" );
	printf( "	minimum x: %3f\n", g_frames[0].mins[0] );
	printf( "	maximum x: %3f\n", g_frames[0].maxs[0] );
	printf( "	minimum y: %3f\n", g_frames[0].mins[1] );
	printf( "	maximum y: %3f\n", g_frames[0].maxs[1] );
	printf( "	minimum z: %3f\n", g_frames[0].mins[2] );
	printf( "	maximum z: %3f\n", g_frames[0].maxs[2] );
	printf( "%4d vertices\n", model.num_xyz );
	printf( "%4d triangles\n", model.num_tris );
	printf( "%4d frame\n", model.num_frames );
	printf( "%4d glverts\n", numglverts );
	printf( "%4d glcmd\n", model.num_glcmds );
	printf( "%4d skins\n", model.num_skins );
	printf( "file size: %d\n", (int)ftell( modelouthandle ) );
	printf( "---------------------\n" );

	fclose( modelouthandle );

	// finish writing header file
	H_printf( "\n" );

	// scale_up is usefull to allow step distances to be adjusted
	H_printf( "#define MODEL_SCALE\t\t%f\n", scale_up );

	fclose( headerouthandle );
	headerouthandle = NULL;
}


/*
   =================================================================

   ALIAS MODEL DISPLAY LIST GENERATION

   =================================================================
 */

int strip_xyz[128];
int strip_st[128];
int strip_tris[128];
int stripcount;

/*
   ================
   StripLength
   ================
 */
static int  StripLength( int starttri, int startv ){
	int m1, m2;
	int st1, st2;
	int j;
	dtriangle_t *last, *check;
	int k;

	used[starttri] = 2;

	last = &triangles[starttri];

	strip_xyz[0] = last->index_xyz[( startv ) % 3];
	strip_xyz[1] = last->index_xyz[( startv + 1 ) % 3];
	strip_xyz[2] = last->index_xyz[( startv + 2 ) % 3];
	strip_st[0] = last->index_st[( startv ) % 3];
	strip_st[1] = last->index_st[( startv + 1 ) % 3];
	strip_st[2] = last->index_st[( startv + 2 ) % 3];

	strip_tris[0] = starttri;
	stripcount = 1;

	m1 = last->index_xyz[( startv + 2 ) % 3];
	st1 = last->index_st[( startv + 2 ) % 3];
	m2 = last->index_xyz[( startv + 1 ) % 3];
	st2 = last->index_st[( startv + 1 ) % 3];

	// look for a matching triangle
nexttri:
	for ( j = starttri + 1, check = &triangles[starttri + 1]
		  ; j < model.num_tris ; j++, check++ )
	{
		for ( k = 0 ; k < 3 ; k++ )
		{
			if ( check->index_xyz[k] != m1 ) {
				continue;
			}
			if ( check->index_st[k] != st1 ) {
				continue;
			}
			if ( check->index_xyz[ ( k + 1 ) % 3 ] != m2 ) {
				continue;
			}
			if ( check->index_st[ ( k + 1 ) % 3 ] != st2 ) {
				continue;
			}

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if ( used[j] ) {
				goto done;
			}

			// the new edge
			if ( stripcount & 1 ) {
				m2 = check->index_xyz[ ( k + 2 ) % 3 ];
				st2 = check->index_st[ ( k + 2 ) % 3 ];
			}
			else
			{
				m1 = check->index_xyz[ ( k + 2 ) % 3 ];
				st1 = check->index_st[ ( k + 2 ) % 3 ];
			}

			strip_xyz[stripcount + 2] = check->index_xyz[ ( k + 2 ) % 3 ];
			strip_st[stripcount + 2] = check->index_st[ ( k + 2 ) % 3 ];
			strip_tris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for ( j = starttri + 1 ; j < model.num_tris ; j++ )
		if ( used[j] == 2 ) {
			used[j] = 0;
		}

	return stripcount;
}


/*
   ===========
   FanLength
   ===========
 */
static int  FanLength( int starttri, int startv ){
	int m1, m2;
	int st1, st2;
	int j;
	dtriangle_t *last, *check;
	int k;

	used[starttri] = 2;

	last = &triangles[starttri];

	strip_xyz[0] = last->index_xyz[( startv ) % 3];
	strip_xyz[1] = last->index_xyz[( startv + 1 ) % 3];
	strip_xyz[2] = last->index_xyz[( startv + 2 ) % 3];
	strip_st[0] = last->index_st[( startv ) % 3];
	strip_st[1] = last->index_st[( startv + 1 ) % 3];
	strip_st[2] = last->index_st[( startv + 2 ) % 3];

	strip_tris[0] = starttri;
	stripcount = 1;

	m1 = last->index_xyz[( startv + 0 ) % 3];
	st1 = last->index_st[( startv + 0 ) % 3];
	m2 = last->index_xyz[( startv + 2 ) % 3];
	st2 = last->index_st[( startv + 2 ) % 3];


	// look for a matching triangle
nexttri:
	for ( j = starttri + 1, check = &triangles[starttri + 1]
		  ; j < model.num_tris ; j++, check++ )
	{
		for ( k = 0 ; k < 3 ; k++ )
		{
			if ( check->index_xyz[k] != m1 ) {
				continue;
			}
			if ( check->index_st[k] != st1 ) {
				continue;
			}
			if ( check->index_xyz[ ( k + 1 ) % 3 ] != m2 ) {
				continue;
			}
			if ( check->index_st[ ( k + 1 ) % 3 ] != st2 ) {
				continue;
			}

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if ( used[j] ) {
				goto done;
			}

			// the new edge
			m2 = check->index_xyz[ ( k + 2 ) % 3 ];
			st2 = check->index_st[ ( k + 2 ) % 3 ];

			strip_xyz[stripcount + 2] = m2;
			strip_st[stripcount + 2] = st2;
			strip_tris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for ( j = starttri + 1 ; j < model.num_tris ; j++ )
		if ( used[j] == 2 ) {
			used[j] = 0;
		}

	return stripcount;
}



/*
   ================
   BuildGlCmds

   Generate a list of trifans or strips
   for the model, which holds for all frames
   ================
 */
static void BuildGlCmds( void ){
	int i, j, k;
	int startv;
	float s, t;
	int len, bestlen, besttype;
	int best_xyz[1024];
	int best_st[1024];
	int best_tris[1024];
	int type;

	//
	// build tristrips
	//
	numcommands = 0;
	numglverts = 0;
	memset( used, 0, sizeof( used ) );
	for ( i = 0 ; i < model.num_tris ; i++ )
	{
		// pick an unused triangle and start the trifan
		if ( used[i] ) {
			continue;
		}

		bestlen = 0;
		for ( type = 0 ; type < 2 ; type++ )
//	type = 1;
		{
			for ( startv = 0 ; startv < 3 ; startv++ )
			{
				if ( type == 1 ) {
					len = StripLength( i, startv );
				}
				else{
					len = FanLength( i, startv );
				}
				if ( len > bestlen ) {
					besttype = type;
					bestlen = len;
					for ( j = 0 ; j < bestlen + 2 ; j++ )
					{
						best_st[j] = strip_st[j];
						best_xyz[j] = strip_xyz[j];
					}
					for ( j = 0 ; j < bestlen ; j++ )
						best_tris[j] = strip_tris[j];
				}
			}
		}

		// mark the tris on the best strip/fan as used
		for ( j = 0 ; j < bestlen ; j++ )
			used[best_tris[j]] = 1;

		if ( besttype == 1 ) {
			commands[numcommands++] = ( bestlen + 2 );
		}
		else{
			commands[numcommands++] = -( bestlen + 2 );
		}

		numglverts += bestlen + 2;

		for ( j = 0 ; j < bestlen + 2 ; j++ )
		{
			// emit a vertex into the reorder buffer
			k = best_st[j];

			// emit s/t coords into the commands stream
			s = base_st[k].s;
			t = base_st[k].t;

			s = ( s + 0.5 ) / model.skinwidth;
			t = ( t + 0.5 ) / model.skinheight;

			*(float *)&commands[numcommands++] = s;
			*(float *)&commands[numcommands++] = t;
			*(int *)&commands[numcommands++] = best_xyz[j];
		}
	}

	commands[numcommands++] = 0;        // end of list marker
}


/*
   ===============================================================

   BASE FRAME SETUP

   ===============================================================
 */


//==========================================================================
//
// DrawScreen
//
//==========================================================================

void DrawScreen( float s_scale, float t_scale, float iwidth, float iheight ){
	int i;
	byte *scrpos;
	char buffer[256];

	// Divider
	scrpos = &pic[( INFO_Y - 2 ) * SKINPAGE_WIDTH];
	for ( i = 0; i < SKINPAGE_WIDTH; i++ )
	{
		*scrpos++ = 255;
	}

	sprintf( buffer, "GENSKIN:  " );
	DrawTextChar( 16, INFO_Y, buffer );

	sprintf( buffer, "( %03d * %03d )   SCALE %f %f, SKINWIDTH %d,"
					 " SKINHEIGHT %d", (int)ScaleWidth, (int)ScaleHeight, s_scale, t_scale, (int)iwidth * 2, (int)iheight );
	DrawTextChar( 80, INFO_Y, buffer );
}

/*
   ============
   BuildST

   Builds the triangle_st array for the base frame and
   model.skinwidth / model.skinheight

   FIXME: allow this to be loaded from a file for
   arbitrary mappings
   ============
 */
void BuildST( triangle_t *ptri, int numtri, qboolean DrawSkin ){
	int i, j;
	int width, height, iwidth, iheight, swidth;
	float basex, basey;
	float scale;
	vec3_t mins, maxs;
	float       *pbasevert;
	vec3_t vtemp1, vtemp2, normal;
	float s_scale, t_scale;
	float scWidth;
	float scHeight;

	//
	// find bounds of all the verts on the base frame
	//
	ClearBounds( mins, maxs );

	for ( i = 0 ; i < numtri ; i++ )
		for ( j = 0 ; j < 3 ; j++ )
			AddPointToBounds( ptri[i].verts[j], mins, maxs );

	for ( i = 0 ; i < 3 ; i++ )
	{
		mins[i] = floor( mins[i] );
		maxs[i] = ceil( maxs[i] );
	}

	width = maxs[0] - mins[0];
	height = maxs[2] - mins[2];


	scWidth = ( ScaleWidth / 2 ) * SCALE_ADJUST_FACTOR;
	scHeight = ScaleHeight * SCALE_ADJUST_FACTOR;

	scale = scWidth / width;

	if ( height * scale >= scHeight ) {
		scale = scHeight / height;
	}

	iwidth = ceil( width * scale ) + 4;
	iheight = ceil( height * scale ) + 4;

	s_scale = (float)( iwidth - 4 ) / width;
	t_scale = (float)( iheight - 4 ) / height;
	t_scale = s_scale;

	if ( DrawSkin ) {
		DrawScreen( s_scale, t_scale, iwidth, iheight );
	}


/*	if (!g_fixedwidth)
    {	// old style
        scale = 8;
        if (width*scale >= 150)
            scale = 150.0 / width;
        if (height*scale >= 190)
            scale = 190.0 / height;

        s_scale = t_scale = scale;

        iwidth = ceil(width*s_scale);
        iheight = ceil(height*t_scale);

        iwidth += 4;
        iheight += 4;
    }
    else
    {	// new style
        iwidth = g_fixedwidth / 2;
        iheight = g_fixedheight;

        s_scale = (float)(iwidth-4) / width;
        t_scale = (float)(iheight-4) / height;
    }*/

//
// determine which side of each triangle to map the texture to
//
	for ( i = 0 ; i < numtri ; i++ )
	{
		if ( ptri[i].HasUV ) {
			for ( j = 0 ; j < 3 ; j++ )
			{
				triangle_st[i][j][0] = Q_rint( ptri[i].uv[j][0] * iwidth );
				triangle_st[i][j][1] = Q_rint( ( 1.0f - ptri[i].uv[j][1] ) * iheight );
			}
		}
		else
		{
			VectorSubtract( ptri[i].verts[0], ptri[i].verts[1], vtemp1 );
			VectorSubtract( ptri[i].verts[2], ptri[i].verts[1], vtemp2 );
			CrossProduct( vtemp1, vtemp2, normal );

			if ( normal[1] > 0 ) {
				basex = iwidth + 2;
			}
			else
			{
				basex = 2;
			}
			basey = 2;

			for ( j = 0 ; j < 3 ; j++ )
			{
				pbasevert = ptri[i].verts[j];

				triangle_st[i][j][0] = Q_rint( ( pbasevert[0] - mins[0] ) * s_scale + basex );
				triangle_st[i][j][1] = Q_rint( ( maxs[2] - pbasevert[2] ) * t_scale + basey );
			}
		}

		DrawLine( triangle_st[i][0][0], triangle_st[i][0][1],
				  triangle_st[i][1][0], triangle_st[i][1][1] );
		DrawLine( triangle_st[i][1][0], triangle_st[i][1][1],
				  triangle_st[i][2][0], triangle_st[i][2][1] );
		DrawLine( triangle_st[i][2][0], triangle_st[i][2][1],
				  triangle_st[i][0][0], triangle_st[i][0][1] );
	}

// make the width a multiple of 4; some hardware requires this, and it ensures
// dword alignment for each scan

	swidth = iwidth * 2;
	model.skinwidth = ( swidth + 3 ) & ~3;
	model.skinheight = iheight;
}


static void ReplaceClusterIndex( int newIndex, int oldindex, int **clusters,
								 IntListNode_t **vertLists, int *num_verts, int *new_num_verts ){
	int i, j;
	IntListNode_t *next;

	for ( j = 0; j < num_verts[0]; ++j )
	{
		for ( i = 0; i < num_verts[j + 1]; ++i )
		{
			if ( clusters[j][i] == oldindex ) {
				++new_num_verts[j + 1];

				next = vertLists[j];

				vertLists[j] = (IntListNode_t *) SafeMalloc( sizeof( IntListNode_t ), "ReplaceClusterIndex" );
				// Currently freed in WriteJointedModelFile only

				vertLists[j]->data = newIndex;
				vertLists[j]->next = next;
			}
		}
	}
}

/*
   =================
   Cmd_Base
   =================
 */
void Cmd_Base( void ){
	vec3_t base_xyz[MAX_VERTS];
	triangle_t  *ptri;
	int i, j, k;
	char file1[1024];
	char file2[1024];

	GetScriptToken( false );

	if ( g_skipmodel || g_release || g_archive ) {
		return;
	}

	printf( "---------------------\n" );
	sprintf( file1, "%s/%s", cdpartial, token );
	printf( "%s  ", file1 );

	ExpandPathAndArchive( file1 );

	sprintf( file1, "%s/%s", cddir, token );
//
// load the base triangles
//
	if ( do3ds ) {
		Load3DSTriangleList( file1, &ptri, &model.num_tris, NULL, NULL );
	}
	else{
		LoadTriangleList( file1, &ptri, &model.num_tris, NULL, NULL );
	}


	GetScriptToken( false );
	sprintf( file2, "%s/%s.pcx", cddir, token );
//	sprintf (trans_file, "%s/!%s_a.pcx", cddir, token);

	printf( "skin: %s\n", file2 );
	Load256Image( file2, &BasePixels, &BasePalette, &BaseWidth, &BaseHeight );

	if ( BaseWidth != SKINPAGE_WIDTH || BaseHeight != SKINPAGE_HEIGHT ) {
		if ( g_allow_newskin ) {
			ScaleWidth = BaseWidth;
			ScaleHeight = BaseHeight;
		}
		else
		{
			Error( "Invalid skin page size: (%d,%d) should be (%d,%d)",
				   BaseWidth,BaseHeight,SKINPAGE_WIDTH,SKINPAGE_HEIGHT );
		}
	}
	else
	{
		ScaleWidth = (float)ExtractNumber( BasePixels, ENCODED_WIDTH_X,
										   ENCODED_WIDTH_Y );
		ScaleHeight = (float)ExtractNumber( BasePixels, ENCODED_HEIGHT_X,
											ENCODED_HEIGHT_Y );
	}

//
// get the ST values
//
	BuildST( ptri, model.num_tris,false );

//
// run through all the base triangles, storing each unique vertex in the
// base vertex list and setting the indirect triangles to point to the base
// vertices
//
	for ( i = 0 ; i < model.num_tris ; i++ )
	{
		for ( j = 0 ; j < 3 ; j++ )
		{
			// get the xyz index
			for ( k = 0 ; k < model.num_xyz ; k++ )
				if ( VectorCompare( ptri[i].verts[j], base_xyz[k] ) ) {
					break;
				}           // this vertex is already in the base vertex list

			if ( k == model.num_xyz ) { // new index
				VectorCopy( ptri[i].verts[j], base_xyz[model.num_xyz] );

				if ( clustered ) {
					ReplaceClusterIndex( k, ptri[i].indicies[j], (int **)&clusters, (IntListNode_t **)&vertLists, (int *)&num_verts, (int *)&new_num_verts );
				}

				model.num_xyz++;
			}

			triangles[i].index_xyz[j] = k;

			// get the st index
			for ( k = 0 ; k < model.num_st ; k++ )
				if ( triangle_st[i][j][0] == base_st[k].s
					 && triangle_st[i][j][1] == base_st[k].t ) {
					break;
				}           // this vertex is already in the base vertex list

			if ( k == model.num_st ) { // new index
				base_st[model.num_st].s = triangle_st[i][j][0];
				base_st[model.num_st].t = triangle_st[i][j][1];
				model.num_st++;
			}

			triangles[i].index_st[j] = k;
		}
	}

	// build triangle strips / fans
	BuildGlCmds();
}

//===============================================================

char    *FindFrameFile( char *frame ){
	int time1;
	char file1[1024];
	static char retname[1024];
	char base[32];
	char suffix[32];
	char            *s;

	if ( strstr( frame, "." ) ) {
		return frame;       // allready in dot format

	}
	// split 'run1' into 'run' and '1'
	s = frame + strlen( frame ) - 1;

	while ( s != frame && *s >= '0' && *s <= '9' )
		s--;

	strcpy( suffix, s + 1 );
	strcpy( base, frame );
	base[s - frame + 1] = 0;

	sprintf( file1, "%s/%s%s.%s",cddir, base, suffix, "hrc" );
	time1 = FileTime( file1 );
	if ( time1 != -1 ) {
		sprintf( retname, "%s%s.%s", base, suffix, "hrc" );
		return retname;
	}

	sprintf( file1, "%s/%s%s.%s",cddir, base, suffix, "asc" );
	time1 = FileTime( file1 );
	if ( time1 != -1 ) {
		sprintf( retname, "%s%s.%s", base, suffix, "asc" );
		return retname;
	}

	sprintf( file1, "%s/%s%s.%s",cddir, base, suffix, "tri" );
	time1 = FileTime( file1 );
	if ( time1 != -1 ) {
		sprintf( retname, "%s%s.%s", base, suffix, "tri" );
		return retname;
	}

	sprintf( file1, "%s/%s%s.%s",cddir, base, suffix, "3ds" );
	time1 = FileTime( file1 );
	if ( time1 != -1 ) {
		sprintf( retname, "%s%s.%s", base, suffix, "3ds" );
		return retname;
	}

	sprintf( file1, "%s/%s%s.%s",cddir, base, suffix, "htr" );
	time1 = FileTime( file1 );
	if ( time1 != -1 ) {
		sprintf( retname, "%s%s.%s", base, suffix, "htr" );
		return retname;
	}

	// check for 'run.1'
	sprintf( file1, "%s/%s.%s",cddir, base, suffix );
	time1 = FileTime( file1 );
	if ( time1 != -1 ) {
		sprintf( retname, "%s.%s", base, suffix );
		return retname;
	}

	Error( "frame %s could not be found",frame );
	return NULL;
}

/*
   ===============
   GrabFrame
   ===============
 */
static void GrabFrame( char *frame ){
	triangle_t      *ptri;
	int i, j;
	trivert_t       *ptrivert;
	int num_tris;
	char file1[1024];
	frame_t         *fr;
	vertexnormals_t vnorms[MAX_VERTS];
	int index_xyz;
	char            *framefile;

	// the frame 'run1' will be looked for as either
	// run.1 or run1.tri, so the new alias sequence save
	// feature an be used
	framefile = FindFrameFile( frame );

	sprintf( file1, "%s/%s", cdarchive, framefile );
	ExpandPathAndArchive( file1 );

	sprintf( file1, "%s/%s",cddir, framefile );

	printf( "grabbing %s  ", file1 );

	if ( model.num_frames >= MAX_FRAMES ) {
		Error( "model.num_frames >= MAX_FRAMES" );
	}
	fr = &g_frames[model.num_frames];
	model.num_frames++;

	strcpy( fr->name, frame );

//
// load the frame
//
	if ( do3ds ) {
		Load3DSTriangleList( file1, &ptri, &num_tris, NULL, NULL );
	}
	else{
		LoadTriangleList( file1, &ptri, &num_tris, NULL, NULL );
	}

	if ( num_tris != model.num_tris ) {
		Error( "%s: number of triangles doesn't match base frame\n", file1 );
	}

//
// allocate storage for the frame's vertices
//
	ptrivert = fr->v;

	for ( i = 0 ; i < model.num_xyz ; i++ )
	{
		vnorms[i].numnormals = 0;
		VectorClear( vnorms[i].normalsum );
	}
	ClearBounds( fr->mins, fr->maxs );

//
// store the frame's vertices in the same order as the base. This assumes the
// triangles and vertices in this frame are in exactly the same order as in the
// base
//
	for ( i = 0 ; i < num_tris ; i++ )
	{
		vec3_t vtemp1, vtemp2, normal;
		float ftemp;

		VectorSubtract( ptri[i].verts[0], ptri[i].verts[1], vtemp1 );
		VectorSubtract( ptri[i].verts[2], ptri[i].verts[1], vtemp2 );
		CrossProduct( vtemp1, vtemp2, normal );

		VectorNormalize( normal, normal );

		// rotate the normal so the model faces down the positive x axis
		ftemp = normal[0];
		normal[0] = -normal[1];
		normal[1] = ftemp;

		for ( j = 0 ; j < 3 ; j++ )
		{
			index_xyz = triangles[i].index_xyz[j];

			// rotate the vertices so the model faces down the positive x axis
			// also adjust the vertices to the desired origin
			ptrivert[index_xyz].v[0] = ( ( -ptri[i].verts[j][1] ) * scale_up ) +
									   adjust[0];
			ptrivert[index_xyz].v[1] = ( ptri[i].verts[j][0] * scale_up ) +
									   adjust[1];
			ptrivert[index_xyz].v[2] = ( ptri[i].verts[j][2] * scale_up ) +
									   adjust[2];

			AddPointToBounds( ptrivert[index_xyz].v, fr->mins, fr->maxs );

			VectorAdd( vnorms[index_xyz].normalsum, normal, vnorms[index_xyz].normalsum );
			vnorms[index_xyz].numnormals++;
		}
	}

//
// calculate the vertex normals, match them to the template list, and store the
// index of the best match
//
	for ( i = 0 ; i < model.num_xyz ; i++ )
	{
		int j;
		vec3_t v;
		float maxdot;
		int maxdotindex;
		int c;

		c = vnorms[i].numnormals;
		if ( !c ) {
			Error( "Vertex with no triangles attached" );
		}

		VectorScale( vnorms[i].normalsum, 1.0 / c, v );
		VectorNormalize( v, v );

		maxdot = -999999.0;
		maxdotindex = -1;

		for ( j = 0 ; j < NUMVERTEXNORMALS ; j++ )
		{
			float dot;

			dot = DotProduct( v, avertexnormals[j] );
			if ( dot > maxdot ) {
				maxdot = dot;
				maxdotindex = j;
			}
		}

		ptrivert[i].lightnormalindex = maxdotindex;
	}

	free( ptri );
}

/*
   ===============
   GrabJointedFrame
   ===============
 */
void GrabJointedFrame( char *frame ){
	char file1[1024];
	char    *framefile;
	frame_t     *fr;

	framefile = FindFrameFile( frame );

	sprintf( file1, "%s/%s", cdarchive, framefile );
	ExpandPathAndArchive( file1 );

	sprintf( file1, "%s/%s",cddir, framefile );

	printf( "grabbing %s\n", file1 );

	fr = &g_frames[model.num_frames - 1]; // last frame read in

	LoadJointList( file1, fr->joints, jointed );
}

/*
   ===============
   GrabGlobals
   ===============
 */
void GrabGlobals( char *frame ){
	char file1[1024];
	char    *framefile;
	frame_t     *fr;

	framefile = FindFrameFile( frame );

	sprintf( file1, "%s/%s", cdarchive, framefile );
	ExpandPathAndArchive( file1 );

	sprintf( file1, "%s/%s",cddir, framefile );

	printf( "grabbing %s\n", file1 );

	fr = &g_frames[model.num_frames - 1]; // last frame read in

	LoadGlobals( file1 );
}

/*
   ===============
   Cmd_Frame
   ===============
 */
void Cmd_Frame( void ){
	while ( ScriptTokenAvailable() )
	{
		GetScriptToken( false );
		if ( g_skipmodel ) {
			continue;
		}
		if ( g_release || g_archive ) {
			model.num_frames = 1;   // don't skip the writeout
			continue;
		}

		H_printf( "#define FRAME_%-16s\t%i\n", token, model.num_frames );

		GrabFrame( token );
	}
}

/*
   ===============
   Cmd_Skin

   Skins aren't actually stored in the file, only a reference
   is saved out to the header file.
   ===============
 */
void Cmd_Skin( void ){
	byte    *palette;
	byte    *pixels;
	int width, height;
	byte    *cropped;
	int y;
	char name[1024], savename[1024];

	GetScriptToken( false );

	if ( model.num_skins == MAX_MD2SKINS ) {
		Error( "model.num_skins == MAX_MD2SKINS" );
	}

	if ( g_skipmodel ) {
		return;
	}

	sprintf( name, "%s/%s.pcx", cddir, token );
	sprintf( savename, "%s/!%s.pcx", g_outputDir, token );
	sprintf( g_skins[model.num_skins], "%s/!%s.pcx", cdpartial, token );

	model.num_skins++;

	if ( g_skipmodel || g_release || g_archive ) {
		return;
	}

	// load the image
	printf( "loading %s\n", name );
	Load256Image( name, &pixels, &palette, &width, &height );
//	RemapZero (pixels, palette, width, height);

	// crop it to the proper size
	cropped = (byte *) SafeMalloc( model.skinwidth * model.skinheight, "Cmd_Skin" );
	for ( y = 0 ; y < model.skinheight ; y++ )
	{
		memcpy( cropped + y * model.skinwidth,
				pixels + y * width, model.skinwidth );
	}

	// save off the new image
	printf( "saving %s\n", savename );
	CreatePath( savename );
	WritePCXfile( savename, cropped, model.skinwidth,
				  model.skinheight, palette );

	free( pixels );
	free( palette );
	free( cropped );
}


/*
   =================
   Cmd_Origin
   =================
 */
void Cmd_Origin( void ){
	// rotate points into frame of reference so model points down the
	// positive x axis
	GetScriptToken( false );
	adjust[1] = -atof( token );

	GetScriptToken( false );
	adjust[0] = atof( token );

	GetScriptToken( false );
	adjust[2] = -atof( token );
}


/*
   =================
   Cmd_ScaleUp
   =================
 */
void Cmd_ScaleUp( void ){
	GetScriptToken( false );
	scale_up = atof( token );
	if ( g_skipmodel || g_release || g_archive ) {
		return;
	}

	printf( "Scale up: %f\n", scale_up );
}


/*
   =================
   Cmd_Skinsize

   Set a skin size other than the default
   =================
 */
void Cmd_Skinsize( void ){
	GetScriptToken( false );
	g_fixedwidth = atoi( token );
	GetScriptToken( false );
	g_fixedheight = atoi( token );
}

/*
   =================
   Cmd_Modelname

   Gives a different name/location for the file, instead of the cddir
   =================
 */
void Cmd_Modelname( void ){
	GetScriptToken( false );
	strcpy( modelname, token );
}

/*
   ===============
   Cmd_Cd
   ===============
 */
void Cmd_Cd( void ){
	char temp[256];

	FinishModel();
	ClearModel();

	GetScriptToken( false );

	// this is a silly mess...
	sprintf( cdpartial, "models/%s", token );
	sprintf( cdarchive, "%smodels/%s", gamedir + strlen( qdir ), token );
	sprintf( cddir, "%s%s", gamedir, cdpartial );

	// Since we also changed directories on the output side (for mirror) make sure the outputdir is set properly too.
	sprintf( temp, "%s%s", g_outputDir, cdpartial );
	strcpy( g_outputDir, temp );

	// if -only was specified and this cd doesn't match,
	// skip the model (you only need to match leading chars,
	// so you could regrab all monsters with -only monsters)
	if ( !g_only[0] ) {
		return;
	}
	if ( strncmp( token, g_only, strlen( g_only ) ) ) {
		g_skipmodel = true;
		printf( "skipping %s\n", cdpartial );
	}
}

/*
   =================
   Cmd_Cluster
   =================
 */
void Cmd_Cluster(){
	char file1[1024];

	GetScriptToken( false );

	printf( "---------------------\n" );
	sprintf( file1, "%s/%s", cdpartial, token );
	printf( "%s\n", file1 );

	ExpandPathAndArchive( file1 );

	sprintf( file1, "%s/%s", cddir, token );

	LoadClusters( file1, (int **)&clusters, (int *)&num_verts, jointed );

	new_num_verts[0] = num_verts[0];

	clustered = 1;
}

// Model construction cover functions.
void MODELCMD_Modelname( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	Cmd_Modelname();
/*
    switch(modeltype)
    {
    case MODEL_MD2:
        Cmd_Modelname ();
        break;
    case MODEL_FM:
        Cmd_FMModelname ();
        break;
    }
 */
}

void MODELCMD_Cd( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		Cmd_Cd();
		break;
	case MODEL_FM:
		Cmd_FMCd();
		break;
	}
}

void MODELCMD_Origin( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	Cmd_Origin();
/*	switch(modeltype)
    {
    case MODEL_MD2:
        Cmd_Origin ();
        break;
    case MODEL_FM:
        Cmd_FMOrigin ();
        break;
    }
 */
}

void MODELCMD_Cluster( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		Cmd_Cluster();
		break;
	case MODEL_FM:
		Cmd_FMCluster();
		break;
	}
}

void MODELCMD_Base( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		Cmd_Base();
		break;
	case MODEL_FM:
		Cmd_FMBase( false );
		break;
	}
}

void MODELCMD_BaseST( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		Cmd_Base();
		break;
	case MODEL_FM:
		Cmd_FMBase( true );
		break;
	}
}

void MODELCMD_ScaleUp( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	Cmd_ScaleUp();
/*	switch(modeltype)
    {
    case MODEL_MD2:
        Cmd_ScaleUp ();
        break;
    case MODEL_FM:
        Cmd_FMScaleUp ();
        break;
    }
 */
}

void MODELCMD_Frame( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		Cmd_Frame();
		break;
	case MODEL_FM:
		Cmd_FMFrame();
		break;
	}
}

void MODELCMD_Skin( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		Cmd_Skin();
		break;
	case MODEL_FM:
		Cmd_FMSkin();
		break;
	}
}

void MODELCMD_Skinsize( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	Cmd_Skinsize();
/*
    switch(modeltype)
    {
    case MODEL_MD2:
        Cmd_Skinsize ();
        break;
    case MODEL_FM:
        Cmd_FMSkinsize ();
        break;
    }
 */
}

void MODELCMD_Skeleton( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		break;
	case MODEL_FM:
		Cmd_FMSkeleton();
		break;
	}
}

void MODELCMD_BeginGroup( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		break;
	case MODEL_FM:
		Cmd_FMBeginGroup();
		break;
	}
}

void MODELCMD_EndGroup( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		break;
	case MODEL_FM:
		Cmd_FMEndGroup();
		break;
	}
}

void MODELCMD_Referenced( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		break;
	case MODEL_FM:
		Cmd_FMReferenced();
		break;
	}
}

void MODELCMD_NodeOrder( int modeltype ){
	if ( g_forcemodel ) {
		modeltype = g_forcemodel;
	}

	switch ( modeltype )
	{
	case MODEL_MD2:
		break;
	case MODEL_FM:
		Cmd_FMNodeOrder();
		break;
	}
}
