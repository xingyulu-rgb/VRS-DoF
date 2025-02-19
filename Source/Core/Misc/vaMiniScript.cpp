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

#include "vaMiniScript.h"
#include "..\System\vaFileStream.h"
#include "..\vaStringTools.h"

using namespace Vanilla;

vaMiniScript::vaMiniScript( ) 
{
    assert( std::this_thread::get_id() == m_mainThreadID );
}
vaMiniScript::~vaMiniScript( )
{
    assert( std::this_thread::get_id() == m_mainThreadID );
    assert( !m_active );
}

bool vaMiniScript::Start( const std::function< void( vaMiniScriptInterface & ) > & scriptFunction )
{
    assert( std::this_thread::get_id() == m_mainThreadID );

    {    
        std::unique_lock<std::mutex> lk( m_mutex );
        assert( !m_active );
        if( m_active )
            return false;
    }

    m_scriptFunction    = scriptFunction;
    m_UIFunction        = nullptr;
    m_lastDeltaTime     = 0.0f;

    // start with execution being owned by script thread - it will give it back as soon as it starts up
    assert( m_currentOwnership == vaMiniScript::EO_Inactive );
    m_currentOwnership  = EO_ScriptThread;

    m_scriptThread      = std::thread( &vaMiniScript::ScriptThread, this );
    m_scriptThreadID    = m_scriptThread.get_id();

    // wait to get back ownership
    {
        std::unique_lock<std::mutex> lk( m_mutex );
        m_active            = true;
        m_cv.wait(lk, [owner = &m_currentOwnership]{ return *owner == EO_MainThread; });
        assert( m_currentOwnership == EO_MainThread );
    }

    return true;
}

void vaMiniScript::TickScript( float deltaTime )
{
    assert( std::this_thread::get_id() == m_mainThreadID );
    {    
        std::unique_lock<std::mutex> lk( m_mutex );
        if( !m_active )
            return;
    }

    // change ownership to the script thread
    {    
        std::unique_lock<std::mutex> lk( m_mutex );
        m_lastDeltaTime = deltaTime;
        assert( m_currentOwnership == EO_MainThread );
        m_currentOwnership = vaMiniScript::EO_ScriptThread;
    }

    // notify/wake script thread if needed
    m_cv.notify_one( );
    
    // wait to get back ownership
    {    
        std::unique_lock<std::mutex> lk( m_mutex );
        m_cv.wait(lk, [owner = &m_currentOwnership]{ return *owner == EO_MainThread; });
        assert( m_currentOwnership == EO_MainThread );
        if( !m_active )
        {
            m_currentOwnership = EO_Inactive;
            m_scriptThread.join();
            m_scriptThreadID = m_scriptThread.get_id();
        }
    }

    // return & continue running game loop
}

void vaMiniScript::TickUI( )
{
    assert( std::this_thread::get_id() == m_mainThreadID );
    if( !m_active || m_UIFunction == nullptr )
        return;

    m_UIFunction( );
}

void vaMiniScript::Stop( )
{
    assert( std::this_thread::get_id() == m_mainThreadID );

    if( !m_active )
        return;

    m_stopRequested = true;
    TickScript( 0.0f );
    assert( !m_active );
}

bool vaMiniScript::YieldExecution( )
{
    assert( std::this_thread::get_id() == m_scriptThreadID );

    // change ownership to main thread
    {    
        std::unique_lock<std::mutex> lk( m_mutex );
        assert( m_currentOwnership == EO_ScriptThread );
        m_currentOwnership = vaMiniScript::EO_MainThread;
    }

    // notify/wake main thread if needed
    m_cv.notify_one( );
    
    // wait to get back ownership
    {    
        std::unique_lock<std::mutex> lk( m_mutex );
        m_cv.wait(lk, [owner = &m_currentOwnership]{ return *owner == EO_ScriptThread; });
        assert( m_currentOwnership == EO_ScriptThread );
    }

    // return & continue running script until next YieldExecution( )
    return !m_stopRequested;
}

bool vaMiniScript::YieldExecutionFor( float deltaTime )
{
    do 
    { 
        if( !YieldExecution() ) return false; 
    } while( (deltaTime = deltaTime - GetDeltaTime()) > 0 );
    return true;
}

bool vaMiniScript::YieldExecutionFor( int numberOfFrames )
{
    do 
    { 
        if( !YieldExecution() ) return false; 
    } while( (numberOfFrames = numberOfFrames - 1) > 0 );
    return true;
}

void vaMiniScript::ScriptThread( )
{
    m_scriptThreadID    = std::this_thread::get_id();

    // wait for our turn (first call to TickScript)
    YieldExecution();

    m_scriptFunction( *static_cast<vaMiniScriptInterface*>(this) );

    // mark as inactive and change ownership to main thread
    {    
        m_scriptFunction    = nullptr;
        m_UIFunction        = nullptr;
        m_active            = false;

        std::unique_lock<std::mutex> lk( m_mutex );
        assert( m_currentOwnership == EO_ScriptThread );
        m_currentOwnership = vaMiniScript::EO_MainThread;
    }

    // notify/wake main thread if needed
    m_cv.notify_one( );
}