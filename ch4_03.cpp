#include <GL/freeglut.h>
#include <MSHTML.h>
#include <NuiApi.h>
#include <FaceTrackLib.h>
#include <iostream>
#include <sstream>
#include "common/GLUtilities.h"

INuiSensor* context = NULL;
HANDLE colorStreamHandle = NULL;
HANDLE depthStreamHandle = NULL;
TextureObject* colorTexture = NULL;
TextureObject* packedDepthTexture = NULL;

IFTFaceTracker* tracker = NULL;
IFTResult* faceResult = NULL;
FT_SENSOR_DATA sensorData;
RECT faceRect;
bool isFaceTracked = false;

bool initializeKinect()
{
    int numKinects = 0;
    HRESULT hr = NuiGetSensorCount( &numKinects );
    if ( FAILED(hr) || numKinects<=0 )
    {
        std::cout << "No Kinect device found." << std::endl;
        return false;
    }
    
    hr = NuiCreateSensorByIndex( 0, &context );
    if ( FAILED(hr) )
    {
        std::cout << "Failed to connect to Kinect device." << std::endl;
        return false;
    }
    
    DWORD nuiFlags = NUI_INITIALIZE_FLAG_USES_SKELETON | NUI_INITIALIZE_FLAG_USES_COLOR |
                     NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX;
    hr = context->NuiInitialize( nuiFlags );
    if ( FAILED(hr) )
    {
        std::cout << "Failed to intialize Kinect: " << std::hex << (long)hr << std::dec << std::endl;
        return false;
    }
    
    hr = context->NuiImageStreamOpen( NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480, 0, 2, NULL, &colorStreamHandle );
    if ( FAILED(hr) )
    {
        std::cout << "Unable to create color stream: " << std::hex << (long)hr << std::dec << std::endl;
        return false;
    }
    
    hr = context->NuiImageStreamOpen( NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX, NUI_IMAGE_RESOLUTION_640x480,
                                      0, 2, NULL, &depthStreamHandle );
    if ( FAILED(hr) )
    {
        std::cout << "Unable to create depth stream: " << std::hex << (long)hr << std::dec << std::endl;
        return false;
    }
    
    hr = context->NuiSkeletonTrackingEnable( NULL, 0 );
    if ( FAILED(hr) )
    {
        std::cout << "Unable to start tracking skeleton." << std::endl;
        return false;
    }
    return true;
}

bool initializeFaceTracker()
{
    tracker = FTCreateFaceTracker();
    if ( !tracker )
    {
        std::cout << "Can't create face tracker" << std::endl;
        return false;
    }
    
    FT_CAMERA_CONFIG colorConfig = {640, 480, NUI_CAMERA_COLOR_NOMINAL_FOCAL_LENGTH_IN_PIXELS};
    FT_CAMERA_CONFIG depthConfig = {640, 480, NUI_CAMERA_DEPTH_NOMINAL_FOCAL_LENGTH_IN_PIXELS * 2};
    HRESULT hr = tracker->Initialize( &colorConfig, &depthConfig, NULL, NULL );
    if ( FAILED(hr) )
    {
        std::cout << "Can't initialize face tracker" << std::endl;
        return false;
    }
    
    hr = tracker->CreateFTResult( &faceResult );
    if ( FAILED(hr) )
    {
        std::cout << "Can't create face tracker result" << std::endl;
        return false;
    }
    
    sensorData.pVideoFrame = FTCreateImage();
    sensorData.pDepthFrame = FTCreateImage();
    if ( !sensorData.pDepthFrame || !sensorData.pDepthFrame )
    {
        std::cout << "Can't create color/depth images" << std::endl;
        return false;
    }
    sensorData.pVideoFrame->Attach( 640, 480, (void*)colorTexture->bits, FTIMAGEFORMAT_UINT8_R8G8B8, 640*3 );
    sensorData.pDepthFrame->Attach( 640, 480, (void*)packedDepthTexture->bits, FTIMAGEFORMAT_UINT16_D13P3, 640 );
    sensorData.ZoomFactor = 1.0f;
    sensorData.ViewOffset.x = 0;
    sensorData.ViewOffset.y = 0;
    return true;
}

bool destroyKinect()
{
    if ( context )
    {
        context->NuiShutdown();
        return true;
    }
    return false;
}

bool destroyFaceTracker()
{
    if ( faceResult ) faceResult->Release();
    if ( tracker ) tracker->Release();
    return true;
}

void updateImageFrame( NUI_IMAGE_FRAME& imageFrame, bool isDepthFrame )
{
    INuiFrameTexture* nuiTexture = imageFrame.pFrameTexture;
    NUI_LOCKED_RECT lockedRect;
    nuiTexture->LockRect( 0, &lockedRect, NULL, 0 );
    if ( lockedRect.Pitch!=NULL )
    {
        const BYTE* buffer = (const BYTE*)lockedRect.pBits;
        for ( int i=0; i<480; ++i )
        {
            const BYTE* line = buffer + i * lockedRect.Pitch;
            const USHORT* bufferWord = (const USHORT*)line;
            for ( int j=0; j<640; ++j )
            {
                if ( !isDepthFrame )
                {
                    unsigned char* ptr = colorTexture->bits + 3 * (i * 640 + j);
                    *(ptr + 0) = line[4 * j + 2];
                    *(ptr + 1) = line[4 * j + 1];
                    *(ptr + 2) = line[4 * j + 0];
                }
                else
                {
                    USHORT* ptr = (USHORT*)packedDepthTexture->bits + (i * 640 + j);
                    *ptr = bufferWord[j];
                }
            }
        }
        
        TextureObject* tobj = (isDepthFrame ? packedDepthTexture : colorTexture);
        glBindTexture( GL_TEXTURE_2D, tobj->id );
        glTexImage2D( GL_TEXTURE_2D, 0, tobj->internalFormat, tobj->width, tobj->height,
                      0, tobj->imageFormat, GL_UNSIGNED_BYTE, tobj->bits );
    }
    nuiTexture->UnlockRect( 0 );
}

void update()
{
    NUI_IMAGE_FRAME colorFrame;
    HRESULT hr = context->NuiImageStreamGetNextFrame( colorStreamHandle, 0, &colorFrame );
    if ( SUCCEEDED(hr) )
    {
        updateImageFrame( colorFrame, false );
        context->NuiImageStreamReleaseFrame( colorStreamHandle, &colorFrame );
    }
    
    NUI_IMAGE_FRAME depthFrame;
    hr = context->NuiImageStreamGetNextFrame( depthStreamHandle, 0, &depthFrame );
    if ( SUCCEEDED(hr) )
    {
        updateImageFrame( depthFrame, true );
        context->NuiImageStreamReleaseFrame( depthStreamHandle, &depthFrame );
    }
    
    if ( tracker && faceResult )
    {
        if ( !isFaceTracked )
        {
            hr = tracker->StartTracking( &sensorData, NULL, NULL, faceResult );
            if ( SUCCEEDED(hr) && SUCCEEDED(faceResult->GetStatus()) ) isFaceTracked = true;
        }
        else
        {
            hr = tracker->ContinueTracking( &sensorData, NULL, faceResult );
            if ( FAILED(hr) || FAILED(faceResult->GetStatus()) ) isFaceTracked = false;
        }
    }
    glutPostRedisplay();
}

void render()
{
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClear( GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT );
    glEnable( GL_TEXTURE_2D );
    
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glOrtho( 0.0, 1.0, 0.0, 1.0, -1.0, 1.0 );
    
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    
    // Draw color quad
    GLfloat vertices[][3] = {
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }
    };
    GLfloat texcoords[][2] = {
        {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
    };
    VertexData meshData = { &(vertices[0][0]), NULL, NULL, &(texcoords[0][0]) };
    
    glBindTexture( GL_TEXTURE_2D, colorTexture->id );
    drawSimpleMesh( WITH_POSITION|WITH_TEXCOORD, 4, meshData, GL_QUADS );
    
    // Draw face tracking plane
    if ( isFaceTracked )
        faceResult->GetFaceRect( &faceRect );
    
    float l = (float)faceRect.left / 640.0f;
    float r = (float)faceRect.right / 640.0f;
    float b = 1.0f - (float)faceRect.bottom / 480.0f;
    float t = 1.0f - (float)faceRect.top / 480.0f;
    GLfloat faceVertices[][3] = {
        { l, b, 0.1f }, { r, b, 0.1f }, { r, t, 0.1f }, { l, t, 0.1f }
    };
    VertexData faceData = { &(faceVertices[0][0]), NULL, NULL, NULL };
    
    glDisable( GL_TEXTURE_2D );
    glLineWidth( 5.0f );
    drawSimpleMesh( WITH_POSITION, 4, faceData, GL_LINE_LOOP );
    
    glutSwapBuffers();
}

void reshape( int w, int h )
{
    glViewport( 0, 0, w, h );
}

void keyEvents( unsigned char key, int x, int y )
{
    switch ( key )
    {
    case 27: case 'Q': case 'q':
        glutLeaveMainLoop();
        return;
    }
    glutPostRedisplay();
}

int main( int argc, char** argv )
{
    glutInit( &argc, argv );
    glutInitDisplayMode( GLUT_RGB|GLUT_DOUBLE|GLUT_DEPTH|GLUT_MULTISAMPLE );
    glutCreateWindow( "ch4_03_Face_Tracking" );
    glutFullScreen();
    
    glutIdleFunc( update );
    glutDisplayFunc( render );
    glutReshapeFunc( reshape );
    glutKeyboardFunc( keyEvents );
    
    if ( !initializeKinect() ) return 1;
    colorTexture = createTexture(640, 480, GL_RGB, 3);
    packedDepthTexture = createTexture(640, 480, GL_LUMINANCE_ALPHA, 2);
    if ( !initializeFaceTracker() ) return 1;

    glutMainLoop();
    
    destroyFaceTracker();
    destroyTexture( colorTexture );
    destroyTexture( packedDepthTexture );
    destroyKinect();
    return 0;
}
