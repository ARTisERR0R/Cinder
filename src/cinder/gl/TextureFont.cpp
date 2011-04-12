/*
 Copyright (c) 2011, The Cinder Project: http://libcinder.org All rights reserved.
 This code is intended for use with the Cinder C++ library: http://libcinder.org

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that
 the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and
	the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
	the following disclaimer in the documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
*/

#include "TextureFont.h"

#include "cinder/Text.h"
#include "cinder/ip/Fill.h"
#include "cinder/ip/Premultiply.h"
	#include "cinder/ImageIo.h"
	#include "cinder/Rand.h"
	#include "cinder/Utilities.h"
#if defined( CINDER_COCOA )
	#include "cinder/cocoa/CinderCocoa.h"
	#if defined( CINDER_MAC )
		#include <ApplicationServices/ApplicationServices.h>
	#endif
#endif

#include <set>

using namespace std;

namespace cinder { namespace gl {

#if defined( CINDER_COCOA )
TextureFont::TextureFont( const Font &font, const string &supportedChars, const TextureFont::Format &format )
	: mFont( font ), mFormat( format )
{
	// get the glyph indices we'll need
	vector<Font::Glyph>	tempGlyphs = font.getGlyphs( supportedChars );
	set<Font::Glyph> glyphs( tempGlyphs.begin(), tempGlyphs.end() );
	// determine the max glyph extents
	Vec2f glyphExtents = Vec2f::zero();
	for( set<Font::Glyph>::const_iterator glyphIt = glyphs.begin(); glyphIt != glyphs.end(); ++glyphIt ) {
		Rectf bb = font.getGlyphBoundingBox( *glyphIt );
		glyphExtents.x = std::max( glyphExtents.x, bb.getWidth() );
		glyphExtents.y = std::max( glyphExtents.y, bb.getHeight() );
	}

	glyphExtents.x = ceil( glyphExtents.x );
	glyphExtents.y = ceil( glyphExtents.y );

	int glyphsWide = floor( mFormat.getTextureWidth() / (glyphExtents.x+3) );
	int glyphsTall = floor( mFormat.getTextureHeight() / (glyphExtents.y+5) );	
	uint8_t curGlyphIndex = 0, curTextureIndex = 0;
	Vec2i curOffset = Vec2i::zero();
	CGGlyph renderGlyphs[glyphsWide*glyphsTall];
	CGPoint renderPositions[glyphsWide*glyphsTall];
	Surface surface( mFormat.getTextureWidth(), mFormat.getTextureHeight(), true );
	ip::fill( &surface, ColorA8u( 0, 0, 0, 0 ) );
	ColorA white( 1, 1, 1, 1 );
	::CGContextRef cgContext = cocoa::createCgBitmapContext( surface );
	::CGContextSetRGBFillColor( cgContext, 1, 1, 1, 1 );
	::CGContextSetFont( cgContext, font.getCgFontRef() );
	::CGContextSetFontSize( cgContext, font.getSize() );
	::CGContextSetTextMatrix( cgContext, CGAffineTransformIdentity );

	for( set<Font::Glyph>::const_iterator glyphIt = glyphs.begin(); glyphIt != glyphs.end(); ) {
		GlyphInfo newInfo;
		newInfo.mTextureIndex = curTextureIndex;
		Rectf bb = font.getGlyphBoundingBox( *glyphIt );
		Vec2f ul = curOffset + Vec2f( 0, glyphExtents.y - bb.getHeight() );
		Vec2f lr = curOffset + Vec2f( glyphExtents.x, glyphExtents.y );
		newInfo.mTexCoords = Area( floor( ul.x ), floor( ul.y ), ceil( lr.x ) + 3, ceil( lr.y ) + 2 );
		newInfo.mOriginOffset.x = floor(bb.x1) - 1;
		newInfo.mOriginOffset.y = -(bb.getHeight()-1)-ceil( bb.y1+0.5f );
		mGlyphMap[*glyphIt] = newInfo;
		renderGlyphs[curGlyphIndex] = *glyphIt;
		renderPositions[curGlyphIndex].x = curOffset.x - floor(bb.x1) + 1;
		renderPositions[curGlyphIndex].y = surface.getHeight() - (curOffset.y + glyphExtents.y) - ceil(bb.y1+0.5f);// - ( 1 - fmod( bb.y1, 1.0f ) );
		curOffset += Vec2i( glyphExtents.x + 3, 0 );
		++glyphIt;
		if( ( ++curGlyphIndex == glyphsWide * glyphsTall ) || ( glyphIt == glyphs.end() ) ) {
			::CGContextShowGlyphsAtPositions( cgContext, renderGlyphs, renderPositions, curGlyphIndex );

			if( ! mFormat.getPremultiply() )
				ip::unpremultiply( &surface );
			mTextures.push_back( gl::Texture( surface ) );
			ip::fill( &surface, ColorA8u( 0, 0, 0, 0 ) );			
			curOffset = Vec2i::zero();
			curGlyphIndex = 0;
			++curTextureIndex;
		}
		else if( ( curGlyphIndex ) % glyphsWide == 0 ) { // wrap around
			curOffset.x = 0;
			curOffset.y += glyphExtents.y + 2;
		}
	}

	::CGContextRelease( cgContext );
}

#elif defined( CINDER_MSW )

set<Font::Glyph> getNecessaryGlyphs( const Font &font, const string &supportedChars )
{
	set<Font::Glyph> result;

	GCP_RESULTS gcpResults;
	unsigned int i;
	WCHAR *glyphIndices = NULL;

	wstring utf16 = toUtf16( supportedChars );

	::SelectObject( Font::getGlobalDc(), font.getHfont() );

	gcpResults.lStructSize = sizeof(gcpResults);
	gcpResults.lpOutString = NULL;
	gcpResults.lpOrder = NULL;
	gcpResults.lpCaretPos = NULL;
	gcpResults.lpClass = NULL;

	uint32_t bufferSize = std::max<uint32_t>( utf16.length() * 1.2, 16);		// Initially guess number of chars plus a few
	while( true ) {
		if( glyphIndices ) {
			free( glyphIndices );
			glyphIndices = NULL;
		}

		glyphIndices = (WCHAR*)malloc( bufferSize * sizeof(WCHAR) );
		gcpResults.nGlyphs = bufferSize;
		gcpResults.lpDx = 0;
		gcpResults.lpGlyphs = glyphIndices;

		if( ! ::GetCharacterPlacementW( Font::getGlobalDc(), utf16.c_str(), utf16.length(), 0,
						&gcpResults, GCP_LIGATE | GCP_DIACRITIC | GCP_GLYPHSHAPE | GCP_REORDER ) ) {
			return set<Font::Glyph>(); // failure
		}

		if( gcpResults.lpGlyphs )
			break;

		// Too small a buffer, try again
		bufferSize += bufferSize / 2;
		if( bufferSize > INT_MAX) {
			return set<Font::Glyph>(); // failure
		}
	}

	for( int i = 0; i < gcpResults.nGlyphs; i++ )
		result.insert( glyphIndices[i] );

	if( glyphIndices )
		free( glyphIndices );

	return result;
}

TextureFont::TextureFont( const Font &font, const string &utf8Chars )
	: mFont( font )
{
	// get the glyph indices we'll need
	set<Font::Glyph> glyphs = getNecessaryGlyphs( font, utf8Chars );
	// determine the max glyph extents
	Vec2i glyphExtents = Vec2f::zero();
	for( set<Font::Glyph>::const_iterator glyphIt = glyphs.begin(); glyphIt != glyphs.end(); ++glyphIt ) {
		Rectf bb = font.getGlyphBoundingBox( *glyphIt );
		glyphExtents.x = std::max<int>( glyphExtents.x, bb.getWidth() );
		glyphExtents.y = std::max<int>( glyphExtents.y, bb.getHeight() );
	}

const int textureWidth = 1024;
const int textureHeight = 1024;

	::SelectObject( Font::getGlobalDc(), mFont.getHfont() );

	int glyphsWide = textureWidth / glyphExtents.x;
	int glyphsTall = textureHeight / glyphExtents.y;	
	uint8_t curGlyphIndex = 0, curTextureIndex = 0;
	Vec2i curOffset = Vec2i::zero();

	Channel channel( textureWidth, textureHeight );
	ip::fill<uint8_t>( &channel, 0 );

	GLYPHMETRICS gm = { 0, };
	MAT2 identityMatrix = { {0,1},{0,0},{0,0},{0,1} };
	size_t bufferSize = 1;
	BYTE *pBuff = new BYTE[bufferSize];
	for( set<Font::Glyph>::const_iterator glyphIt = glyphs.begin(); glyphIt != glyphs.end(); ) {
		DWORD dwBuffSize = ::GetGlyphOutline( Font::getGlobalDc(), *glyphIt, GGO_GRAY8_BITMAP | GGO_GLYPH_INDEX, &gm, 0, NULL, &identityMatrix );
		if( dwBuffSize > bufferSize ) {
			pBuff = new BYTE[dwBuffSize];
			bufferSize = dwBuffSize;
		}

		if( ::GetGlyphOutline( Font::getGlobalDc(), *glyphIt, GGO_METRICS | GGO_GLYPH_INDEX, &gm, 0, NULL, &identityMatrix ) == GDI_ERROR ) {
			continue;
		}

		if( ::GetGlyphOutline( Font::getGlobalDc(), *glyphIt, GGO_GRAY8_BITMAP | GGO_GLYPH_INDEX, &gm, dwBuffSize, pBuff, &identityMatrix ) == GDI_ERROR ) {
			continue;
		}

		// convert 6bit to 8bit gray
		for( INT p = 0; p < dwBuffSize; ++p )
			pBuff[p] = ((uint32_t)pBuff[p]) * 255 / 64;

		int32_t alignedRowBytes = ( gm.gmBlackBoxX & 3 ) ? ( gm.gmBlackBoxX + 4 - ( gm.gmBlackBoxX & 3 ) ) : gm.gmBlackBoxX;
		Channel glyphChannel( gm.gmBlackBoxX, gm.gmBlackBoxY, alignedRowBytes, 1, pBuff );
		channel.copyFrom( glyphChannel, glyphChannel.getBounds(), curOffset );

		GlyphInfo newInfo;
		newInfo.mOriginOffset = Vec2f( gm.gmptGlyphOrigin.x, glyphExtents.y - gm.gmptGlyphOrigin.y );
		newInfo.mTexCoords = Area( curOffset, curOffset + Vec2i( gm.gmBlackBoxX, gm.gmBlackBoxY ) );
		newInfo.mTextureIndex = curTextureIndex;
		mGlyphMap[*glyphIt] = newInfo;

		curOffset += Vec2i( glyphExtents.x, 0 );
		++glyphIt;
		if( ( ++curGlyphIndex == glyphsWide * glyphsTall ) || ( glyphIt == glyphs.end() ) ) {
			Surface tempSurface( channel, SurfaceConstraintsDefault(), true );
			tempSurface.getChannelAlpha().copyFrom( channel, channel.getBounds() );
			ip::unpremultiply( &tempSurface );
			mTextures.push_back( gl::Texture( tempSurface ) );
			ip::fill<uint8_t>( &channel, 0 );			
			curOffset = Vec2i::zero();
			curGlyphIndex = 0;
			++curTextureIndex;
		}
		else if( ( curGlyphIndex ) % glyphsWide == 0 ) { // wrap around
			curOffset.x = 0;
			curOffset.y += glyphExtents.y;
		}
	}

	delete [] pBuff;
}
#endif

void TextureFont::drawGlyphs( const vector<pair<uint16_t,Vec2f> > &glyphMeasures, const Vec2f &baseline )
{
	SaveTextureBindState saveBindState( mTextures[0].getTarget() );
	BoolState saveEnabledState( mTextures[0].getTarget() );
	ClientBoolState vertexArrayState( GL_VERTEX_ARRAY );
	ClientBoolState texCoordArrayState( GL_TEXTURE_COORD_ARRAY );	
	gl::enable( mTextures[0].getTarget() );

	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	for( size_t texIdx = 0; texIdx < mTextures.size(); ++texIdx ) {
		vector<float> verts, texCoords;
#if defined( CINDER_GLES )
		vector<uint16_t> indices;
		uint16_t curIdx = 0;
		GLenum indexType = GL_UNSIGNED_SHORT;
#else
		vector<uint32_t> indices;
		uint32_t curIdx = 0;
		GLenum indexType = GL_UNSIGNED_INT;
#endif
		for( vector<pair<uint16_t,Vec2f> >::const_iterator glyphIt = glyphMeasures.begin(); glyphIt != glyphMeasures.end(); ++glyphIt ) {
			std::map<Font::Glyph, GlyphInfo>::const_iterator glyphInfoIt = mGlyphMap.find( glyphIt->first );
			if( (glyphInfoIt == mGlyphMap.end()) || (mGlyphMap[glyphIt->first].mTextureIndex != texIdx) )
				continue;
				
			const GlyphInfo &glyphInfo = glyphInfoIt->second;
			
			Rectf destRect( glyphInfo.mTexCoords );
			Rectf srcCoords = mTextures[texIdx].getAreaTexCoords( glyphInfo.mTexCoords );
			destRect -= destRect.getUpperLeft();
			destRect += glyphIt->second;
			destRect += Vec2f( floor( glyphInfo.mOriginOffset.x + 0.5f ), floor( glyphInfo.mOriginOffset.y ) );
			destRect += Vec2f( baseline.x, baseline.y - mFont.getAscent() );
			destRect.offset( -Vec2f( destRect.x1 - floor( destRect.x1 ), destRect.y1 - floor( destRect.y1 ) ) );
			
			verts.push_back( destRect.getX2() ); verts.push_back( destRect.getY1() );
			verts.push_back( destRect.getX1() ); verts.push_back( destRect.getY1() );
			verts.push_back( destRect.getX2() ); verts.push_back( destRect.getY2() );
			verts.push_back( destRect.getX1() ); verts.push_back( destRect.getY2() );

			texCoords.push_back( srcCoords.getX2() ); texCoords.push_back( srcCoords.getY1() );
			texCoords.push_back( srcCoords.getX1() ); texCoords.push_back( srcCoords.getY1() );
			texCoords.push_back( srcCoords.getX2() ); texCoords.push_back( srcCoords.getY2() );
			texCoords.push_back( srcCoords.getX1() ); texCoords.push_back( srcCoords.getY2() );
			
			indices.push_back( curIdx + 0 ); indices.push_back( curIdx + 1 ); indices.push_back( curIdx + 2 );
			indices.push_back( curIdx + 2 ); indices.push_back( curIdx + 1 ); indices.push_back( curIdx + 3 );
			curIdx += 4;
		}
		
		if( curIdx == 0 )
			continue;
		
		mTextures[texIdx].bind();
		glVertexPointer( 2, GL_FLOAT, 0, &verts[0] );
		glTexCoordPointer( 2, GL_FLOAT, 0, &texCoords[0] );
		glDrawElements( GL_TRIANGLES, indices.size(), indexType, &indices[0] );
	}
}

void TextureFont::drawString( const std::string &str, const Vec2f &baseline )
{
	TextBox tbox = TextBox().font( mFont ).text( str ).size( TextBox::GROW, TextBox::GROW );
	vector<pair<uint16_t,Vec2f> > glyphMeasures = tbox.measureGlyphs();
	drawGlyphs( glyphMeasures, baseline );
}

void TextureFont::drawString( const std::string &str, const Rectf &fitRect, const Vec2f &offset )
{
	TextBox tbox = TextBox().font( mFont ).text( str ).size( fitRect.getWidth(), fitRect.getHeight() );
	vector<pair<uint16_t,Vec2f> > glyphMeasures = tbox.measureGlyphs();
	drawGlyphs( glyphMeasures, fitRect.getUpperLeft() );	
}

} } // namespace cinder::gl