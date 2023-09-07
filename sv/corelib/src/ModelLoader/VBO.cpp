/*
 *
 * Copyright © 2017 NXP
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "ModelLoader/VBO.hpp"

VBO::VBO(Vertex *vertices, Vertex *normals, Coord *texcoords, int _count, int _matId) {
    this->count = _count;
    this->matId = _matId;
    this->buffer[0] = 0;
    this->buffer[1] = 0;
    this->buffer[2] = 0;
    this->buffer[3] = 0;
    this->vao = 0;

    // Vertex array
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Allocate and assign three VBO to our handle (vertices, normals and texture coordinates)
    glGenBuffers(4, buffer);

    // store vertices into buffer
    glBindBuffer(GL_ARRAY_BUFFER, buffer[P_VERTEX]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * count, vertices, GL_STATIC_DRAW);
    // vertices are on index 0 and contains three floats per vertex
    glVertexAttribPointer(GLuint(0), 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    // store normals into buffer
    glBindBuffer(GL_ARRAY_BUFFER, buffer[P_NORMAL]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * count, normals, GL_STATIC_DRAW);
    // normals are on index 1 and contains three floats per vertex
    glVertexAttribPointer(GLuint(1), 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);

    // store texture coordinates
    glBindBuffer(GL_ARRAY_BUFFER, buffer[P_TEXCOORD]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Coord) * count, texcoords, GL_STATIC_DRAW);

    // coordinates are on index 2 and contains two floats per vertex
    glVertexAttribPointer(GLuint(2), 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

VBO::~VBO() {}

/*
TexturePtr TextureCache::CreateFromImage( const char* _filename )
{
        if( Exist( _filename ) )
        {
                return GetPtr( _filename );
        }

        //FIXME: tohle nejak nastavit z venku
        bool mipmap = true;
        bool aniso = true;

        //-----------------------------------------------------------------------------
        // Texture, LoadImage
        if(!_filename)
                return 0;

        //Opens image
        if(!m_is_IL_Initialized)
        {
                ilInit();
                m_is_IL_Initialized = true;
        }

        ILuint image_id = 0;
        ilGenImages(1, &image_id);
        ilBindImage(image_id);

        if(!ilLoadImage(_filename))
        {
                ShowMessage("Cannot open texture file!");
                return 0;
        }

        //Get image attributes
        int width, height, bpp;
        width = ilGetInteger(IL_IMAGE_WIDTH);
        height = ilGetInteger(IL_IMAGE_HEIGHT);
        bpp = ilGetInteger(IL_IMAGE_BPP);

        if((width <= 0) || (height <= 0) )
        {
                ShowMessage("Unknown image type!");
                return false;
        }

        //Allocate memory for image
        unsigned imageSize = (bpp * width * height);
        GLubyte* imageData = new GLubyte[imageSize];

        if(imageData == NULL)
        {
                ShowMessage("Can't allocate image data!");
                return false;
        }

        //Copy pixels
        if(bpp==1)
                ilCopyPixels(0, 0, 0, width, height, 1, IL_LUMINANCE, IL_UNSIGNED_BYTE, imageData);
        else if(bpp==3)
                ilCopyPixels(0, 0, 0, width, height, 1, IL_RGB, IL_UNSIGNED_BYTE, imageData);
        else if(bpp==4)
                ilCopyPixels(0, 0, 0, width, height, 1, IL_RGBA, IL_UNSIGNED_BYTE, imageData);

        ilBindImage( 0 );
        ilDeleteImage( image_id );

        //-----------------------------------------------------------------------------

        TexturePtr tex = new Texture( TEX_2D );
        tex->Bind();

        //texture with anisotropic filtering
        if(aniso)
        {
                //find out, if GFX supports aniso filtering
                if(!GLEW_EXT_texture_filter_anisotropic)
                {
                        cout<<"Anisotropic filtering not supported. Using linear instead.\n";
                }
                else
                {
                        float maxAnisotropy;
                        //find out maximum supported anisotropy
                        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
                        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
maxAnisotropy);
                }
        }
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        //mip-mapped texture (if set)
        if(mipmap)
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        else
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);

        if(tex->GetType() == BUMP)	//don't compress bump maps
#ifdef _LINUX_
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_BGR,
GL_UNSIGNED_BYTE, imageData); #else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
GL_UNSIGNED_BYTE, imageData); #endif else					//compress color
maps #ifdef _LINUX_ glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB, m_width, m_height, 0, GL_BGR,
GL_UNSIGNED_BYTE, imageData); #else glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB, width, height,
0, GL_RGB, GL_UNSIGNED_BYTE, imageData); #endif glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
GL_REPEAT); glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); if(mipmap)
                glGenerateMipmap(GL_TEXTURE_2D);

        //Unbind and free
        tex->Unbind();

        delete [] imageData;

        cout<<"Image loaded: "<< _filename <<"\n";

        //-- add created textures into cache
        Add( _filename, tex );

        tex->SetSize( width, height );
        tex->SetBpp( bpp );
        tex->SetName( "image" );

        return tex;
}
*/
