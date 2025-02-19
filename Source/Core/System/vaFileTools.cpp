///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016, Intel Corporation
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

#include "vaFileTools.h"
#include "vaFileStream.h"
#include "Core/vaMemory.h"
#include "Core/System/vaMemoryStream.h"

#include "Core/vaStringTools.h"

#include <stdio.h>

using namespace Vanilla;

#pragma warning ( disable: 4996 )

std::map<wstring, vaFileTools::EmbeddedFileData> vaFileTools::s_EmbeddedFiles;

bool vaFileTools::FileExists( const wstring & path )
{
    FILE * fp = _wfopen( path.c_str( ), L"rb" );
    if( fp != NULL )
    {
        fclose( fp );
        return true;
    }
    return false;
}

bool vaFileTools::DeleteFile( const wstring & path )
{
    int ret = ::_wremove( path.c_str( ) );
    return ret == 0;
}

bool vaFileTools::MoveFile( const wstring & oldPath, const wstring & newPath )
{
    int ret = ::_wrename( oldPath.c_str( ), newPath.c_str( ) );
    return ret == 0;
}

std::shared_ptr<vaMemoryStream> vaFileTools::LoadFileToMemoryStream( const string & fileName )
{
    return LoadFileToMemoryStream( vaStringTools::SimpleWiden( fileName ) );
}

std::shared_ptr<vaMemoryStream> vaFileTools::LoadFileToMemoryStream( const wstring & fileName )
{
    vaFileStream file;
    if( !file.Open( fileName, FileCreationMode::Open ) )
        return std::shared_ptr<vaMemoryStream>( nullptr );

    int64 length = file.GetLength( );

    if( length == 0 )
        return std::shared_ptr<vaMemoryStream>( nullptr );

    std::shared_ptr<vaMemoryStream> ret( new vaMemoryStream( length ) );

    if( !file.Read( ret->GetBuffer( ), ret->GetLength( ) ) )
        return std::shared_ptr<vaMemoryStream>( nullptr );

    return ret;
}

string vaFileTools::LoadFileAsText( const wstring& fileName )
{
    auto stream = LoadFileToMemoryStream( fileName );
    if( stream == nullptr )
        return "";
    string retVal = "";
    if( stream->ReadTXT( retVal ) )
        return retVal;
    return "";
}

wstring vaFileTools::CleanupPath( const wstring & inputPath, bool convertToLowercase, bool useBackslash )
{
    wstring ret = inputPath;
    if( convertToLowercase )
        ret = vaStringTools::ToLower( ret );

    wstring::size_type foundPos;
    while( ( foundPos = ret.find( L"/" ) ) != wstring::npos )
        ret.replace( foundPos, 1, L"\\" );
    while( ( foundPos = ret.find( L"\\\\" ) ) != wstring::npos )
        ret.replace( foundPos, 2, L"\\" );

    // restore network path
    if( ( ret.length( ) > 0 ) && ( ret[0] == '\\' ) )
        ret = L"\\" + ret;

    if( !useBackslash )
        while( ( foundPos = ret.find( L"\\" ) ) != wstring::npos )
            ret.replace( foundPos, 1, L"/" );

    return ret;
}

wstring vaFileTools::GetAbsolutePath( const wstring & path )
{
#define BUFSIZE 32770
    DWORD  retval = 0;
    TCHAR  buffer[BUFSIZE] = TEXT( "" );
    TCHAR** lppPart = { NULL };

    wstring lpp = L"\\\\?\\";
    wstring longPath = lpp + path;

    // Retrieve the full path name for a file. 
    // The file does not need to exist.
    retval = GetFullPathName( longPath.c_str(), BUFSIZE, buffer, lppPart );

    if( retval == 0 )
    {
        VA_ASSERT_ALWAYS( L"Failed getting absolute path to '%s'", path.c_str( ) );
        return L"";
    }
    else
    {
        wstring absolutePath = buffer;
        auto lpti = absolutePath.find( lpp );
        if( lpti != wstring::npos )
            absolutePath = absolutePath.substr( lpti + lpp.length() );
        return absolutePath;
    }
}

static void FindFilesRecursive( const wstring & startDirectory, const wstring & searchName, bool recursive, std::vector<wstring> & outResult )
{
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;

    wstring combined = startDirectory + searchName;

    hFind = FindFirstFileExW( combined.c_str( ), FindExInfoBasic, &FindFileData, FindExSearchNameMatch, NULL, 0 );
    bool bFound = hFind != INVALID_HANDLE_VALUE;
    while( bFound )
    {
        if( //( ( FindFileData.dwFileAttributes & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL) ) != 0 ) &&
             ( ( FindFileData.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_VIRTUAL) ) == 0 ) )
            outResult.push_back( startDirectory + FindFileData.cFileName );

        bFound = FindNextFileW( hFind, &FindFileData ) != 0;
    }

    if( recursive )
    {
        combined = startDirectory + L"*";

        FindClose( hFind );
        hFind = FindFirstFileExW( combined.c_str( ), FindExInfoBasic, &FindFileData, FindExSearchLimitToDirectories, NULL, 0 );
        bFound = hFind != INVALID_HANDLE_VALUE;
        while( bFound )
        {
            if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
            {
                wstring subDir = FindFileData.cFileName;
                if( subDir != L"." && subDir != L".." )
                {
                    FindFilesRecursive( startDirectory + subDir + L"\\", searchName, recursive, outResult );
                }
            }

            bFound = FindNextFileW( hFind, &FindFileData ) != 0;
        }
    }
    if( INVALID_HANDLE_VALUE != hFind )
        FindClose( hFind );
}

std::vector<wstring> vaFileTools::FindFiles( const wstring & startDirectory, const wstring & searchName, bool recursive )
{
    std::vector<wstring> result;

    FindFilesRecursive( startDirectory, searchName, recursive, result );

    return result;
}

std::vector<wstring> vaFileTools::FindDirectories( const wstring & startDirectory )
{
    std::vector<wstring> result;

    WIN32_FIND_DATA FindFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;

    wstring combined = startDirectory + L"*";

    FindClose( hFind );
    hFind = FindFirstFileExW( combined.c_str( ), FindExInfoBasic, &FindFileData, FindExSearchLimitToDirectories, NULL, 0 );
    bool bFound = hFind != INVALID_HANDLE_VALUE;
    while( bFound )
    {
        if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
        {
            wstring subDir = FindFileData.cFileName;
            if( subDir != L"." && subDir != L".." )
            {
                result.push_back( startDirectory + FindFileData.cFileName );
            }
        }
        bFound = FindNextFileW( hFind, &FindFileData ) != 0;
    }

    if( INVALID_HANDLE_VALUE != hFind )
        FindClose( hFind );
    return result;
}

void                      vaFileTools::EmbeddedFilesRegister( const wstring & _pathName, byte * data, int64 dataSize, int64 timeStamp )
{
    // case insensitive
    wstring pathName = vaStringTools::ToLower( _pathName );

    std::map<wstring, EmbeddedFileData>::iterator it = s_EmbeddedFiles.find( pathName );

    if( it != s_EmbeddedFiles.end( ) )
    {
        VA_WARN( L"Embedded file %s already registered!", pathName.c_str( ) );
        return;
    }

    s_EmbeddedFiles.insert( std::pair<wstring, EmbeddedFileData>( pathName,
        EmbeddedFileData( pathName, std::shared_ptr<vaMemoryStream>( new vaMemoryStream( data, dataSize ) ), timeStamp ) ) );
}

vaFileTools::EmbeddedFileData vaFileTools::EmbeddedFilesFind( const wstring & _pathName )
{
    // case insensitive
    wstring pathName = vaStringTools::ToLower( _pathName );

    std::map<wstring, EmbeddedFileData>::iterator it = s_EmbeddedFiles.find( pathName );

    if( it != s_EmbeddedFiles.end( ) )
        return it->second;

    return EmbeddedFileData( );
}

void vaFileTools::Initialize( )
{

}

void vaFileTools::Deinitialize( )
{
    for( std::map<wstring, EmbeddedFileData>::iterator it = s_EmbeddedFiles.begin( ); it != s_EmbeddedFiles.end( ); ++it )
    {
        VA_ASSERT( it->second.MemStream.unique( ), L"Embedded file %s reference count not 0, stream not closed but storage no longer guaranteed!", it->first.c_str( ) );
    }

    s_EmbeddedFiles.clear( );
}

bool vaFileTools::ReadBuffer( const wstring & filePath, void * buffer, size_t size )
{
    vaFileStream file;
    if( !file.Open( filePath, FileCreationMode::Open ) )
        return false;

    return file.Read( buffer, size );
}

bool vaFileTools::WriteBuffer( const wstring & filePath, void * buffer, size_t size )
{
    vaFileStream file;
    if( !file.Open( filePath, FileCreationMode::Create ) )
        return false;

    return file.Write( buffer, size );
}

void vaFileTools::SplitPath( const string & inFullPath, string * outDirectory, string * outFileName, string * outFileExt )
{
    char buffDrive[32];
    char buffDir[4096];
    char buffName[4096];
    char buffExt[4096];

    _splitpath_s( inFullPath.c_str( ), buffDrive, _countof( buffDrive ),
        buffDir, _countof( buffDir ), buffName, _countof( buffName ), buffExt, _countof( buffExt ) );

    if( outDirectory != NULL ) *outDirectory = string( buffDrive ) + string( buffDir );
    if( outFileName != NULL )  *outFileName = buffName;
    if( outFileExt != NULL )   *outFileExt = buffExt;
}

void vaFileTools::SplitPath( const wstring & inFullPath, wstring * outDirectory, wstring * outFileName, wstring * outFileExt )
{
    wchar_t buffDrive[32];
    wchar_t buffDir[4096];
    wchar_t buffName[4096];
    wchar_t buffExt[4096];

    //assert( !((outDirectory != NULL) && ( (outDirectory != outFileName) || (outDirectory != outFileExt) )) );
    //assert( !((outFileName != NULL) && (outFileName != outFileExt)) );

    _wsplitpath_s( inFullPath.c_str( ), buffDrive, _countof( buffDrive ),
        buffDir, _countof( buffDir ), buffName, _countof( buffName ), buffExt, _countof( buffExt ) );

    if( outDirectory != NULL ) *outDirectory = wstring( buffDrive ) + wstring( buffDir );
    if( outFileName != NULL )  *outFileName = buffName;
    if( outFileExt != NULL )   *outFileExt = buffExt;
}

string  vaFileTools::SplitPathExt( const string & inFullPath )
{
    string ret;
    SplitPath( inFullPath, nullptr, nullptr, &ret );
    return ret;
}

wstring vaFileTools::SplitPathExt( const wstring & inFullPath )
{
    wstring ret;
    SplitPath( inFullPath, nullptr, nullptr, &ret );
    return ret;
}

wstring vaFileTools::FindLocalFile( const wstring & fileName )
{
    if( vaFileTools::FileExists( ( vaCore::GetWorkingDirectory( ) + fileName ).c_str( ) ) )
        return vaFileTools::CleanupPath( vaCore::GetWorkingDirectory( ) + fileName, false );
    if( vaFileTools::FileExists( ( vaCore::GetExecutableDirectory( ) + fileName ).c_str( ) ) )
        return vaFileTools::CleanupPath( vaCore::GetExecutableDirectory( ) + fileName, false );
    if( vaFileTools::FileExists( fileName.c_str( ) ) )
        return vaFileTools::CleanupPath( fileName, false );

    return L"";
}

wstring vaFileTools::FixExtension( const wstring& path, const wstring & _ext )
{
    wstring ext = _ext;
    if( ext.length() == 0 ) return path;
    if( ext[0] != L'.' )
    {
        assert( false );
        return L""; 
    }
    wstring currentExt;
    SplitPath( path, nullptr, nullptr, &currentExt );
    if( vaStringTools::CompareNoCase( currentExt, ext ) != 0 )
        return path + ext;
    else
        return path;
}

string vaFileTools::OpenFileDialog( const string & initialFileName, const string & initialDir, const char * filter, int filterIndex, const string & dialogTitle )
{
    wstring wfilter = (filter != nullptr)?(vaStringTools::SimpleWiden(filter)):(L"");
    return vaStringTools::SimpleNarrow( OpenFileDialog( vaStringTools::SimpleWiden(initialFileName), vaStringTools::SimpleWiden(initialDir), (wfilter == L"")?(nullptr):(wfilter.c_str()), filterIndex, vaStringTools::SimpleWiden(dialogTitle) ) );
}

string vaFileTools::SaveFileDialog( const string & fileName, const string & initialDir, const char * filter, int filterIndex, const string & dialogTitle )
{
    wstring wfilter = ( filter != nullptr ) ? ( vaStringTools::SimpleWiden( filter ) ) : ( L"" );
    return vaStringTools::SimpleNarrow( OpenFileDialog( vaStringTools::SimpleWiden( fileName ), vaStringTools::SimpleWiden( initialDir ), ( wfilter == L"" ) ? ( nullptr ) : ( wfilter.c_str( ) ), filterIndex, vaStringTools::SimpleWiden( dialogTitle ) ) );
}

string vaFileTools::SelectFolderDialog( const string & initialDir )
{
    return vaStringTools::SimpleNarrow( SelectFolderDialog( vaStringTools::SimpleWiden( initialDir ) ) );
}

void vaFileTools::OpenSystemExplorerFolder( const string & folderPath )
{
    OpenSystemExplorerFolder( vaStringTools::SimpleWiden( folderPath ) );
}
