/*
 ===========================================================================

 Doom 3 BFG Edition GPL Source Code
 Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 
 
 This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  
 
 Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.
 
 In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.
 
 If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

 ===========================================================================
 */

#pragma hdrstop
#include "../idlib/precompiled.h"


extern idCVar s_noSound;

#include "tr_local.h"

// Linux port debug: log cinematic decode/upload activity (1 line/sec) so videoMap
// screens can be diagnosed from the console log without screenshots.
static idCVar r_debugCinematic( "r_debugCinematic", "0", CVAR_BOOL | CVAR_RENDERER,
	"log idCinematic ImageForTime/UploadCurrentFrame activity once per second" );

//===========================================
// Linux: engine-side Bink (.bik) decoder via libavformat/libavcodec/libswscale.
// The GPL tree stubbed idCinematic (no decoder), so every videoMap / intro .bik
// rendered as a static frame. Stock assets already ship the bink*.glsl YUV
// renderprogs inside _common.resources (samp0=Y, samp1=Cr, samp2=Cb — bound in
// that order by RB_BindVariableStageImage, tr_backend_draw.cpp:353-359), so only
// the decoder was missing. The whole file is read through fileSystem->ReadFile
// (works for loose base/video/*.bik AND for packed .resources entries) and fed
// to ffmpeg through a custom in-memory AVIOContext.
//===========================================
extern "C" {
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/rational.h>
}

class idCinematicLocal : public idCinematic {
public:
						idCinematicLocal();
	virtual				~idCinematicLocal();

	virtual bool		InitFromFile( const char *qpath, bool looping );
	virtual cinData_t	ImageForTime( int milliseconds );
	virtual int			AnimationLength();
	virtual void		Close();
	virtual int			GetStartTime();
	virtual void		ResetTime( int time );
	virtual float		GetFrameRate() const;

private:
	bool				OpenBink( const char *qpath, bool loopingFlag );
	void				ResetStream();
	void				UploadCurrentFrame();
	cinData_t			MakeCinData() const;

	// in-memory file served to ffmpeg through a custom AVIOContext
	static int			AvioRead( void *opaque, uint8_t *buf, int bufSize );
	static int64_t		AvioSeek( void *opaque, int64_t offset, int whence );

	uint8_t *			fileData;
	int64_t				fileSize;
	int64_t				filePos;
	AVIOContext *		avioCtx;
	AVFormatContext *	fmtCtx;
	AVCodecContext *	decCtx;
	const AVCodec *		dec;
	AVFrame *			frame;
	AVPacket *			pkt;
	SwsContext *		swsCtx;
	int					videoStreamIndex;

	uint8_t *			yuvData[4];		// sws output, AV_PIX_FMT_YUV420P planes
	int					yuvLinesize[4];

	idImage *			imgY;
	idImage *			imgCr;			// V plane
	idImage *			imgCb;			// U plane
	bool				imagesAllocated;

	idStr				fileName;
	int					cinWidth;
	int					cinHeight;
	int					numFrames;
	int					animationLength;	// ms
	int					startTime;
	float				frameRate;
	int					framePos;
	bool				hasFrame;
	bool				looping;
	bool				valid;
	cinStatus_t			status;
};

/*
==============
idCinematic::InitCinematic
==============
*/
void idCinematic::InitCinematic( void ) {
}

/*
==============
idCinematic::ShutdownCinematic
==============
*/
void idCinematic::ShutdownCinematic( void ) {
}

/*
==============
idCinematic::Alloc
==============
*/
idCinematic * idCinematic::Alloc() {
	return new (TAG_CINEMATIC) idCinematicLocal;
}

/*
==============
idCinematic::~idCinematic
==============
*/
idCinematic::~idCinematic( ) {
	Close();
}

/*
==============
idCinematic::InitFromFile
==============
*/
bool idCinematic::InitFromFile( const char *qpath, bool looping ) {
	return false;
}

/*
==============
idCinematic::AnimationLength
==============
*/
int idCinematic::AnimationLength() {
	return 0;
}

/*
==============
idCinematic::GetStartTime
==============
*/
int idCinematic::GetStartTime() {
	return -1;
}

/*
==============
idCinematic::ResetTime
==============
*/
void idCinematic::ResetTime(int milliseconds) {
}

/*
==============
idCinematic::ImageForTime
==============
*/
cinData_t idCinematic::ImageForTime( int milliseconds ) {
	cinData_t c;
	memset( &c, 0, sizeof( c ) );
	return c;
}

/*
==============
idCinematic::ExportToTGA
==============
*/
void idCinematic::ExportToTGA( bool skipExisting ) {
}

/*
==============
idCinematic::GetFrameRate
==============
*/
float idCinematic::GetFrameRate() const {
	return 30.0f;
}

/*
==============
idCinematic::Close
==============
*/
void idCinematic::Close() {
}

//===========================================
// idCinematicLocal
//===========================================

/*
==============
idCinematicLocal::idCinematicLocal
==============
*/
idCinematicLocal::idCinematicLocal() {
	fileData = NULL;
	fileSize = 0;
	filePos = 0;
	avioCtx = NULL;
	fmtCtx = NULL;
	decCtx = NULL;
	dec = NULL;
	frame = av_frame_alloc();
	pkt = av_packet_alloc();
	swsCtx = NULL;
	videoStreamIndex = -1;
	memset( yuvData, 0, sizeof( yuvData ) );
	memset( yuvLinesize, 0, sizeof( yuvLinesize ) );
	imgY = globalImages->AllocStandaloneImage( "_cinematicY" );
	imgCr = globalImages->AllocStandaloneImage( "_cinematicCr" );
	imgCb = globalImages->AllocStandaloneImage( "_cinematicCb" );
	imagesAllocated = false;
	cinWidth = 0;
	cinHeight = 0;
	numFrames = 0;
	animationLength = 0;
	startTime = -1;
	frameRate = 30.0f;
	framePos = -1;
	hasFrame = false;
	looping = false;
	valid = false;
	status = FMV_EOF;
}

/*
==============
idCinematicLocal::~idCinematicLocal
==============
*/
idCinematicLocal::~idCinematicLocal() {
	Close();
	if ( frame != NULL ) {
		av_frame_free( &frame );
	}
	if ( pkt != NULL ) {
		av_packet_free( &pkt );
	}
	if ( imgY != NULL ) {
		delete imgY;
		imgY = NULL;
	}
	if ( imgCr != NULL ) {
		delete imgCr;
		imgCr = NULL;
	}
	if ( imgCb != NULL ) {
		delete imgCb;
		imgCb = NULL;
	}
}

/*
==============
idCinematicLocal::AvioRead
==============
*/
int idCinematicLocal::AvioRead( void *opaque, uint8_t *buf, int bufSize ) {
	idCinematicLocal *self = (idCinematicLocal *)opaque;
	int64_t remaining = self->fileSize - self->filePos;
	if ( remaining <= 0 ) {
		return AVERROR_EOF;
	}
	int n = (int)( remaining < bufSize ? remaining : bufSize );
	memcpy( buf, self->fileData + self->filePos, n );
	self->filePos += n;
	return n;
}

/*
==============
idCinematicLocal::AvioSeek
==============
*/
int64_t idCinematicLocal::AvioSeek( void *opaque, int64_t offset, int whence ) {
	idCinematicLocal *self = (idCinematicLocal *)opaque;
	if ( whence == AVSEEK_SIZE ) {
		return self->fileSize;
	}
	int base = whence & ~AVSEEK_FORCE;
	int64_t newPos;
	switch ( base ) {
		case SEEK_SET: newPos = offset; break;
		case SEEK_CUR: newPos = self->filePos + offset; break;
		case SEEK_END: newPos = self->fileSize + offset; break;
		default: return (int64_t)AVERROR( EINVAL );
	}
	if ( newPos < 0 ) {
		newPos = 0;
	}
	if ( newPos > self->fileSize ) {
		newPos = self->fileSize;
	}
	self->filePos = newPos;
	return newPos;
}

/*
==============
idCinematicLocal::InitFromFile
==============
*/
bool idCinematicLocal::InitFromFile( const char *qpath, bool loopingFlag ) {
	Close();
	looping = loopingFlag;

	fileName = qpath;
	fileName.BackSlashesToSlashes();
	fileName.ToLower();
	if ( strstr( fileName.c_str(), "/" ) == NULL ) {
		// no folder specified: look in the video folder
		fileName = va( "video/%s", fileName.c_str() );
	}

	// BFG ships Bink (.bik) files, but many stock defs/materials still reference
	// the original DOOM 3 RoQ (.roq) names — retry with the .bik extension.
	if ( fileSystem->ReadFile( fileName.c_str(), NULL, NULL ) <= 0 && fileName.CheckExtension( "roq" ) ) {
		idStr bikName = fileName;
		bikName.SetFileExtension( "bik" );
		fileName = bikName;
	}

	return OpenBink( fileName.c_str(), loopingFlag );
}

/*
==============
idCinematicLocal::OpenBink
==============
*/
bool idCinematicLocal::OpenBink( const char *qpath, bool loopingFlag ) {
	int len = fileSystem->ReadFile( qpath, (void **)&fileData, NULL );
	if ( len <= 0 || fileData == NULL ) {
		common->Warning( "idCinematic: cannot read video file '%s'\n", qpath );
		return false;
	}
	fileSize = len;
	filePos = 0;

	const int avioBufSize = 64 * 1024;
	uint8_t *avioBuf = (uint8_t *)av_malloc( avioBufSize );
	if ( avioBuf == NULL ) {
		return false;
	}
	avioCtx = avio_alloc_context( avioBuf, avioBufSize, 0, this, &idCinematicLocal::AvioRead, NULL, &idCinematicLocal::AvioSeek );
	if ( avioCtx == NULL ) {
		av_free( avioBuf );
		return false;
	}

	fmtCtx = avformat_alloc_context();
	fmtCtx->pb = avioCtx;
	fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;	// we own avioCtx and free it in Close()

	int ret = avformat_open_input( &fmtCtx, NULL, NULL, NULL );
	if ( ret < 0 ) {
		char err[128];
		av_strerror( ret, err, sizeof( err ) );
		common->Warning( "idCinematic: avformat_open_input failed for '%s': %s\n", qpath, err );
		return false;
	}
	if ( avformat_find_stream_info( fmtCtx, NULL ) < 0 ) {
		common->Warning( "idCinematic: cannot find stream info in '%s'\n", qpath );
		return false;
	}

	ret = av_find_best_stream( fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0 );
	if ( ret < 0 ) {
		common->Warning( "idCinematic: no video stream in '%s'\n", qpath );
		return false;
	}
	videoStreamIndex = ret;
	AVStream *st = fmtCtx->streams[videoStreamIndex];

	decCtx = avcodec_alloc_context3( dec );
	if ( decCtx == NULL ) {
		return false;
	}
	if ( avcodec_parameters_to_context( decCtx, st->codecpar ) < 0 ) {
		common->Warning( "idCinematic: avcodec_parameters_to_context failed for '%s'\n", qpath );
		return false;
	}
	ret = avcodec_open2( decCtx, dec, NULL );
	if ( ret < 0 ) {
		char err[128];
		av_strerror( ret, err, sizeof( err ) );
		common->Warning( "idCinematic: cannot open decoder for '%s': %s\n", qpath, err );
		return false;
	}

	cinWidth = decCtx->width;
	cinHeight = decCtx->height;
	if ( cinWidth <= 0 || cinHeight <= 0 ) {
		common->Warning( "idCinematic: bad video dimensions in '%s'\n", qpath );
		return false;
	}

	AVRational fr = st->avg_frame_rate;
	if ( fr.num <= 0 || fr.den <= 0 ) {
		fr = st->r_frame_rate;
	}
	frameRate = (float)av_q2d( fr );
	if ( frameRate <= 0.0f ) {
		frameRate = 30.0f;
	}

	double durSec = -1.0;
	if ( st->duration > 0 ) {
		durSec = st->duration * av_q2d( st->time_base );
	}
	if ( durSec <= 0.0 && fmtCtx->duration > 0 ) {
		durSec = fmtCtx->duration / (double)AV_TIME_BASE;
	}
	if ( durSec < 0.0 ) {
		durSec = 0.0;
	}
	animationLength = (int)( durSec * 1000.0 + 0.5 );
	numFrames = (int)st->nb_frames;
	if ( numFrames <= 0 ) {
		numFrames = (int)( durSec * frameRate + 0.5 );
	}
	if ( numFrames <= 0 ) {
		numFrames = 1;
	}

	if ( av_image_alloc( yuvData, yuvLinesize, cinWidth, cinHeight, AV_PIX_FMT_YUV420P, 32 ) < 0 ) {
		common->Warning( "idCinematic: av_image_alloc failed for '%s'\n", qpath );
		return false;
	}

	framePos = -1;
	hasFrame = false;
	startTime = Sys_Milliseconds();
	status = FMV_PLAY;
	valid = true;

	common->Printf( "Loaded Bink file: '%s', looping=%d, %dx%d, %.2f FPS, %.1f sec, %d frames\n",
		qpath, (int)loopingFlag, cinWidth, cinHeight, frameRate, durSec, numFrames );
	return true;
}

/*
==============
idCinematicLocal::Close
==============
*/
void idCinematicLocal::Close() {
	valid = false;
	status = FMV_EOF;
	hasFrame = false;
	framePos = -1;

	if ( swsCtx != NULL ) {
		sws_freeContext( swsCtx );
		swsCtx = NULL;
	}
	if ( decCtx != NULL ) {
		avcodec_free_context( &decCtx );
	}
	if ( fmtCtx != NULL ) {
		avformat_close_input( &fmtCtx );	// CUSTOM_IO: does not free our avioCtx
	}
	if ( avioCtx != NULL ) {
		av_freep( &avioCtx->buffer );
		avio_context_free( &avioCtx );
	}
	// note: frame/pkt are allocated in the constructor and freed in the
	// destructor — Close() must stay callable repeatedly (InitFromFile
	// calls it first) without nuking them
	if ( yuvData[0] != NULL ) {
		av_freep( &yuvData[0] );
	}
	memset( yuvLinesize, 0, sizeof( yuvLinesize ) );
	if ( fileData != NULL ) {
		Mem_Free( fileData );
		fileData = NULL;
	}
	fileSize = 0;
	filePos = 0;

	if ( imagesAllocated ) {
		imgY->PurgeImage();
		imgCr->PurgeImage();
		imgCb->PurgeImage();
		imagesAllocated = false;
	}
}

/*
==============
idCinematicLocal::ResetStream
==============
*/
void idCinematicLocal::ResetStream() {
	if ( fmtCtx != NULL ) {
		av_seek_frame( fmtCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD );
	}
	if ( decCtx != NULL ) {
		avcodec_flush_buffers( decCtx );
	}
	framePos = -1;
}

/*
==============
idCinematicLocal::UploadCurrentFrame

Converts the decoded frame to YUV420P and uploads the planes to the three
luminance images. Plane order required by the stock bink*.glsl renderprogs:
samp0 = Y, samp1 = Cr (V plane), samp2 = Cb (U plane).
==============
*/
void idCinematicLocal::UploadCurrentFrame() {
	if ( frame->width <= 0 || frame->height <= 0 ) {
		return;
	}

	if ( !imagesAllocated ) {
		// deferred until the first draw: materials can be parsed before the
		// GL context exists, and idImage::AllocImage needs a live context
		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_LUM8;
		opts.colorFormat = CFM_DEFAULT;
		opts.numLevels = 1;
		opts.width = cinWidth;
		opts.height = cinHeight;
		imgY->AllocImage( opts, TF_LINEAR, TR_REPEAT );
		opts.width = ( cinWidth + 1 ) / 2;
		opts.height = ( cinHeight + 1 ) / 2;
		imgCr->AllocImage( opts, TF_LINEAR, TR_REPEAT );
		imgCb->AllocImage( opts, TF_LINEAR, TR_REPEAT );
		imagesAllocated = true;
	}

	swsCtx = sws_getCachedContext( swsCtx, frame->width, frame->height, (AVPixelFormat)frame->format,
		cinWidth, cinHeight, AV_PIX_FMT_YUV420P, SWS_POINT, NULL, NULL, NULL );
	if ( swsCtx == NULL ) {
		return;
	}
	sws_scale( swsCtx, (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height, yuvData, yuvLinesize );

	imgY->SubImageUpload( 0, 0, 0, 0, cinWidth, cinHeight, yuvData[0], yuvLinesize[0] );
	const int cw = ( cinWidth + 1 ) / 2;
	const int ch = ( cinHeight + 1 ) / 2;
	imgCr->SubImageUpload( 0, 0, 0, 0, cw, ch, yuvData[2], yuvLinesize[2] );	// V = Cr
	imgCb->SubImageUpload( 0, 0, 0, 0, cw, ch, yuvData[1], yuvLinesize[1] );	// U = Cb

	if ( r_debugCinematic.GetBool() ) {
		uint32_t ck = 2166136261u;
		int nb = cinWidth < 64 ? cinWidth : 64;
		for ( int i = 0; i < nb; i++ ) {
			ck = ( ck ^ yuvData[0][i] ) * 16777619u;
		}
		common->Printf( "CINDBG upload %s: frame=%d %dx%d texY=%u texCr=%u texCb=%u ycksum=%08x y0=%02x %02x %02x %02x\n",
			fileName.c_str(), framePos, cinWidth, cinHeight,
			(unsigned) imgY->IsLoaded(), (unsigned) imgCr->IsLoaded(), (unsigned) imgCb->IsLoaded(),
			ck, yuvData[0][0], yuvData[0][1], yuvData[0][2], yuvData[0][3] );
	}
	hasFrame = true;
}

/*
==============
idCinematicLocal::MakeCinData
==============
*/
cinData_t idCinematicLocal::MakeCinData() const {
	cinData_t c;
	c.imageWidth = cinWidth;
	c.imageHeight = cinHeight;
	c.imageY = imgY;
	c.imageCr = imgCr;
	c.imageCb = imgCb;
	c.status = status;
	return c;
}

/*
==============
idCinematicLocal::ImageForTime

milliseconds is absolute time in the same domain as ResetTime() (the engine
passes renderView.time[0] = gameLocal.slow.time; a value <= 0 means "now").
==============
*/
cinData_t idCinematicLocal::ImageForTime( int milliseconds ) {
	cinData_t cinData;
	memset( &cinData, 0, sizeof( cinData ) );

	if ( milliseconds <= 0 ) {
		milliseconds = Sys_Milliseconds();
	}

	if ( !valid || r_skipDynamicTextures.GetBool() || status == FMV_EOF || status == FMV_IDLE ) {
		return cinData;
	}

	if ( !hasFrame || startTime == -1 ) {
		if ( startTime == -1 ) {
			ResetStream();
		}
		startTime = milliseconds;
	}

	int desiredFrame = idMath::Ftoi( ( milliseconds - startTime ) * frameRate / 1000.0f );
	if ( desiredFrame < 0 ) {
		desiredFrame = 0;
	}

	// rewind: map change / PDA re-open / startTime in the future
	if ( desiredFrame < framePos ) {
		ResetStream();
		hasFrame = false;
		status = FMV_PLAY;
	}

	if ( hasFrame && desiredFrame == framePos ) {
		return MakeCinData();
	}

	if ( desiredFrame >= numFrames ) {
		if ( looping ) {
			ResetStream();
			hasFrame = false;
			startTime = milliseconds;
			status = FMV_LOOPED;
			desiredFrame = 0;
		} else {
			hasFrame = false;
			status = FMV_IDLE;
			return cinData;
		}
	}

	int eofHits = 0;
	while ( framePos < desiredFrame ) {
		int ret = av_read_frame( fmtCtx, pkt );
		if ( ret < 0 ) {
			// end of container: drain any frames still buffered in the decoder
			avcodec_send_packet( decCtx, NULL );
			while ( avcodec_receive_frame( decCtx, frame ) == 0 ) {
				framePos++;
				UploadCurrentFrame();
			}
			if ( framePos >= desiredFrame ) {
				break;
			}
			if ( ++eofHits > 2 ) {
				// decoder is not making progress — give up cleanly
				hasFrame = false;
				status = FMV_IDLE;
				return cinData;
			}
			if ( looping ) {
				ResetStream();
				startTime = milliseconds;
				desiredFrame = 0;
				status = FMV_LOOPED;
				continue;
			}
			hasFrame = false;
			status = FMV_IDLE;
			return cinData;
		}
		if ( pkt->stream_index == videoStreamIndex ) {
			if ( avcodec_send_packet( decCtx, pkt ) == 0 ) {
				while ( avcodec_receive_frame( decCtx, frame ) == 0 ) {
					framePos++;
					UploadCurrentFrame();
				}
			}
		}
		av_packet_unref( pkt );
	}

	if ( r_debugCinematic.GetBool() ) {
		static int lastDbg = 0;
		int nowMs = Sys_Milliseconds();
		if ( nowMs - lastDbg > 1000 ) {
			lastDbg = nowMs;
			common->Printf( "CINDBG %s: ms=%d desired=%d framePos=%d hasFrame=%d valid=%d status=%d %dx%d startTime=%d numFrames=%d\n",
				fileName.c_str(), milliseconds, desiredFrame, framePos, (int) hasFrame, (int) valid,
				(int) status, cinWidth, cinHeight, startTime, numFrames );
		}
	}

	if ( !hasFrame ) {
		return cinData;
	}
	return MakeCinData();
}

/*
==============
idCinematicLocal::AnimationLength
==============
*/
int idCinematicLocal::AnimationLength() {
	return animationLength;
}

/*
==============
idCinematicLocal::GetStartTime
==============
*/
int idCinematicLocal::GetStartTime() {
	return startTime;
}

/*
==============
idCinematicLocal::ResetTime
==============
*/
void idCinematicLocal::ResetTime( int time ) {
	startTime = time;
	status = FMV_PLAY;
}

/*
==============
idCinematicLocal::GetFrameRate
==============
*/
float idCinematicLocal::GetFrameRate() const {
	return frameRate;
}

/*
==============
idSndWindow::InitFromFile
==============
*/
bool idSndWindow::InitFromFile( const char *qpath, bool looping ) {
	idStr fname = qpath;

	fname.ToLower();
	if ( !fname.Icmp( "waveform" ) ) {
		showWaveform = true;
	} else {
		showWaveform = false;
	}
	return true;
}

/*
==============
idSndWindow::ImageForTime
==============
*/
cinData_t idSndWindow::ImageForTime( int milliseconds ) {
	return soundSystem->ImageForTime( milliseconds, showWaveform );
}

/*
==============
idSndWindow::AnimationLength
==============
*/
int idSndWindow::AnimationLength() {
	return -1;
}
