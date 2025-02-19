///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2019, Intel Corporation
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated 
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation 
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of 
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "vaCore.h"

#include "Core/Misc/vaBenchmarkTool.h"

#include "Misc/vaProfiler.h"

#include "Core/vaInput.h"

#include "Core/System/vaFileTools.h"

#include "Core/vaUI.h"

#include "Core/vaUIDObject.h"

#include "Rendering/vaRendering.h"

#include "Core/vaApplicationBase.h"

#include "IntegratedExternals/vaGTSIntegration.h"
#include "IntegratedExternals/vaTaskflowIntegration.h"

// for GUIDs
#include <Objbase.h>
#pragma comment( lib, "Ole32.lib" )
#pragma comment( lib, "Rpcrt4.lib" )

using namespace Vanilla;

bool vaCore::s_initialized = false;

bool vaCore::s_appQuitFlag = false;
bool vaCore::s_appQuitButRestartFlag = false;

bool vaCore::s_currentlyInitializing = false;
bool vaCore::s_currentlyDeinitializing = false;

#ifdef VA_USE_NATIVE_WINDOWS_TIMER
LARGE_INTEGER vaCore::s_appStartTime;
LARGE_INTEGER vaCore::s_timerFrequency;
#else
std::chrono::time_point<std::chrono::steady_clock>
vaCore::s_appStartTime;
#endif

vaRandom vaRandom::Singleton;

// int omp_thread_count( )
// {
//     int n = 0;
// #pragma omp parallel reduction(+:n)
//     n += 1;
//     return n;
// }

void vaCore::Initialize( bool liveRestart )
{
    if( !liveRestart )
    {
#ifdef VA_USE_NATIVE_WINDOWS_TIMER
        ::QueryPerformanceCounter(&s_appStartTime);
        ::QueryPerformanceFrequency(&s_timerFrequency);
#else
        s_appStartTime = std::chrono::steady_clock::now( );
#endif
    }

    // Initializing more than once?
    assert( !s_initialized );

    if( liveRestart )
    { assert( vaThreading::IsMainThread() ); }

    if( !liveRestart )
    {
        vaThreading::SetMainThread( );

        vaMemory::Initialize( );

        new vaUIDObjectRegistrar( );

        vaPlatformBase::Initialize( );

        new vaLog( );

        new vaRenderingModuleRegistrar( );
    }

    vaFileTools::Initialize( );

    new vaUIManager( );
    new vaUIConsole( );

    // VA_TRACE_CPU_SCOPE( Initialize );

    // using namespace std::chrono_literals;
    // std::this_thread::sleep_for( 100ms );

    // std::thread::hardware_concurrency()
    int physicalPackages, physicalCores, logicalCores;
    vaThreading::GetCPUCoreCountInfo( physicalPackages, physicalCores, logicalCores );

    // adhoc heuristic for determining the number of worker threads
    // enkiTS actually creates threadsToUse-1 worker threads but if you WaitforXXX from the main thread it will execute queued tasks from the main as well
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    new vaEnkiTS( vaMath::Max( 2, ( physicalCores + logicalCores - 1 ) / 2 ) );
#endif

#ifdef VA_GTS_INTEGRATION_ENABLED
    new vaGTS(logicalCores);
#endif
#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
    new vaTF(std::max(1, logicalCores));
#endif

    new vaBackgroundTaskManager( );


    //   InitializeSubsystemManagers( );
       // hmm not needed at the moment
       // new vaTelemetryServer();


    //   new vaThreadPool( logicalCoresToUse, vaThread::TP_Normal, 256*1024 );

    new vaBenchmarkTool( );

    // useful to make things more deterministic during restarts
    vaRandom::Singleton.Seed(0);

    s_initialized = true;
}

void vaCore::Deinitialize( bool liveRestart )
{
    assert( s_initialized );

    delete vaBenchmarkTool::GetInstancePtr( );

    //   delete vaThreadPool::GetInstancePtr();

    //   DeinitializeSubsystemManagers( );

    delete vaBackgroundTaskManager::GetInstancePtr( );
#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
    delete vaTF::GetInstancePtr();
#endif
#ifdef VA_GTS_INTEGRATION_ENABLED
    delete vaGTS::GetInstancePtr( );
#endif
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    delete vaEnkiTS::GetInstancePtr( );
#endif
    delete vaUIConsole::GetInstancePtr( );
    delete vaUIManager::GetInstancePtr( );
    
    vaFileTools::Deinitialize( );

    if( !liveRestart )
    {
        delete vaRenderingModuleRegistrar::GetInstancePtr( );

        delete vaLog::GetInstancePtr( );

        vaPlatformBase::Deinitialize( );

        delete vaUIDObjectRegistrar::GetInstancePtr( );

        vaTracer::Cleanup( false );

        vaMemory::Deinitialize( );
    }
    else
    {
        vaTracer::Cleanup( false );
    }

    s_initialized = false;
}

void vaCore::Error( const wchar_t * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::wstring ret = vaStringTools::Format( messageFormat, args );
    va_end( args );

    VA_LOG_ERROR( L"%s", ret.c_str( ) );

    vaPlatformBase::Error( ret.c_str() );
}

void vaCore::Warning( const wchar_t * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::wstring ret = vaStringTools::Format( messageFormat, args );
    va_end( args );

    if( vaLog::GetInstancePtr( ) != nullptr )
        VA_LOG_WARNING( L"%s", ret.c_str( ) );

    // vaPlatformBase::Warning( ret.c_str() );
}

void vaCore::DebugOutput( const wstring & message )
{
    vaPlatformBase::DebugOutput( message.c_str( ) );
}

void vaCore::Error( const char * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::string ret = vaStringTools::Format( messageFormat, args );
    va_end( args );

    VA_LOG_ERROR( "%s", ret.c_str( ) );

    // assert( false );
    // vaPlatformBase::Error( ret.c_str() );
}

void vaCore::Warning( const char * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::string ret = vaStringTools::Format( messageFormat, args );
    va_end( args );

    if( vaLog::GetInstancePtr( ) != nullptr )
        VA_LOG_WARNING( "%s", ret.c_str( ) );

    // vaPlatformBase::Warning( ret.c_str() );
}

void vaCore::DebugOutput( const string & message )
{
    vaPlatformBase::DebugOutput( vaStringTools::SimpleWiden( message ).c_str( ) );
}

void vaCore::MessageLoopTick( )
{
    if( vaApplicationBase::GetInstanceValid( ) )
        vaApplicationBase::GetInstance( ).MessageLoopTick( );
}

bool vaCore::MessageBoxYesNo( const wchar_t * title, const wchar_t * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::wstring ret = vaStringTools::Format( messageFormat, args );
    va_end( args );
    return vaPlatformBase::MessageBoxYesNo( title, ret.c_str( ) );
}

vaGUID vaCore::GUIDCreate( )
{
    vaGUID ret;
    ::CoCreateGuid( &ret );
    return ret;
}

vaGUID vaGUID::Create( )
{
    vaGUID ret;
    ::CoCreateGuid( &ret );
    return ret;
}

const vaGUID vaGUID::Null( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 );

const vaGUID & vaCore::GUIDNull( )
{
    static vaGUID null = vaGUID( GUID_NULL );
    static vaGUID null2 = vaGUID::Null;
    return null;
}

wstring vaCore::GUIDToString( const vaGUID & id )
{
    wstring ret;
    wchar_t * buffer;
    RPC_STATUS s = UuidToStringW( &id, (RPC_WSTR*)&buffer );
    VA_ASSERT( s == RPC_S_OK, L"GUIDToString failed" );
    s;  // unreferenced in Release
    ret = buffer;
    RpcStringFree( (RPC_WSTR*)&buffer );
    return ret;
}

vaGUID vaCore::GUIDFromString( const wstring & str )
{
    vaGUID ret;
    RPC_STATUS s = UuidFromStringW( (RPC_WSTR)str.c_str( ), &ret );
    VA_ASSERT( s == RPC_S_OK, L"GUIDFromString failed" );
    if( s != RPC_S_OK )
        return GUIDNull( );
    return ret;
}

string vaGUID::ToString( )
{
    string ret;
    char* buffer;
    RPC_STATUS s = UuidToStringA( this, (RPC_CSTR*)&buffer );
    VA_ASSERT( s == RPC_S_OK, L"GUIDToStringA failed" );
    s;  // unreferenced in Release
    ret = buffer;
    RpcStringFreeA( (RPC_CSTR*)&buffer );
    return ret;
}

string vaCore::GUIDToStringA( const vaGUID & id )
{
    string ret;
    char * buffer;
    RPC_STATUS s = UuidToStringA( &id, (RPC_CSTR*)&buffer );
    VA_ASSERT( s == RPC_S_OK, L"GUIDToStringA failed" );
    s;  // unreferenced in Release
    ret = buffer;
    RpcStringFreeA( (RPC_CSTR*)&buffer );
    return ret;
}

vaGUID vaCore::GUIDFromStringA( const string & str )
{
    vaGUID ret;
    RPC_STATUS s = UuidFromStringA( (RPC_CSTR)str.c_str( ), &ret );
    VA_ASSERT( s == RPC_S_OK, L"GUIDFromString failed" );
    if( s != RPC_S_OK )
        return GUIDNull( );
    return ret;
}

string vaCore::GetWorkingDirectoryNarrow( ) 
{ 
    return vaStringTools::SimpleNarrow( GetWorkingDirectory( ) ); 
}

string vaCore::GetExecutableDirectoryNarrow( ) 
{ 
    return vaStringTools::SimpleNarrow( GetExecutableDirectory( ) ); 
}

string vaCore::GetMediaRootDirectoryNarrow( ) 
{ 
    return vaStringTools::SimpleNarrow( GetMediaRootDirectory( ) ); 
}



vaInputKeyboardBase *    vaInputKeyboardBase::s_current = NULL;
vaInputMouseBase *       vaInputMouseBase::s_current = NULL;
