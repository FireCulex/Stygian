/*
===============================================================================

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

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

===============================================================================
*/

/*
===============================================================================

Linux (SDL) savegame backend.

Synchronous port of sys/win32/win_savegame.cpp.  The win32 build runs these
operations on a worker thread (idSaveGameThread); that thread is never
started on Linux, so Sys_ExecuteSavegameCommandAsyncImpl executes the
operation inline on the calling thread and then raises callbackSignal, which
is exactly what idSaveGameManager::Pump() polls.  Because the work completes
before the signal is raised, ExecuteProcessorAndWait() finishes after at most
two Pump() passes and never hangs.

On-disk layout is identical to win32:
    <fs_savepath>/<gameFolder>/savegame/GAME-<slot>/{game.details, gamedata.save, gamedata.strings}
Binary files get a big-endian MD5 checksum prepended when saveGame_checksum is set.

===============================================================================
*/

#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../sys_savegame.h"
#include "../../framework/File_SaveGame.h"
#include "../../idlib/hashing/MD5.h"

#include <sys/stat.h>

extern idCVar fs_savepath;
extern idCVar saveGame_checksum;
extern idCVar saveGame_verbose;
extern idCVar savegame_error;

#define SGL_ERROR_SUCCESS	0

/*
========================
SGL_Save

Writes all files in parms->files into savegame/<parms->directory>/ using a
temp-file + rename per file so a crash can't leave a half-written save.
========================
*/
static int SGL_Save( idSaveLoadParms * callback ) {
	idStr saveFolder = "savegame";
	saveFolder.AppendPath( callback->directory );

	int ret = SGL_ERROR_SUCCESS;

	// Delete all previous files if needed
	// ALL THE FILES RIGHT NOW----  could use pattern later...
	idStrList filesToDelete;
	if ( ( callback->mode & SAVEGAME_MBF_DELETE_FILES ) && !callback->cancelled ) {
		if ( fileSystem->IsFolder( saveFolder.c_str(), "fs_savePath" ) == FOLDER_YES ) {
			idFileList * files = fileSystem->ListFilesTree( saveFolder.c_str(), "*.*" );
			for ( int i = 0; i < files->GetNumFiles(); i++ ) {
				filesToDelete.Append( files->GetFile( i ) );
			}
			fileSystem->FreeFileList( files );
		}
	}

	for ( int i = 0; i < filesToDelete.Num() && !callback->cancelled; i++ ) {
		fileSystem->RemoveFile( filesToDelete[i].c_str() );
	}

	// Save the raw files.
	for ( int i = 0; i < callback->files.Num() && ret == SGL_ERROR_SUCCESS && !callback->cancelled; i++ ) {
		idFile_SaveGame * file = callback->files[i];

		idStr fileName = saveFolder;
		fileName.AppendPath( file->GetName() );
		idStr tempFileName = va_format( "%s.temp", fileName.c_str() );

		idFile * outputFile = fileSystem->OpenFileWrite( tempFileName, "fs_savePath" );
		if ( outputFile == NULL ) {
			idLib::Warning( "[%s]: Couldn't open file for writing, %s.", __FUNCTION__, tempFileName.c_str() );
			file->error = true;
			callback->errorCode = SAVEGAME_E_UNKNOWN;
			ret = -1;
			continue;
		}

		if ( ( file->type & SAVEGAMEFILE_PIPELINED ) != 0 ) {

			idFile_SaveGamePipelined * inputFile = dynamic_cast< idFile_SaveGamePipelined * >( file );
			assert( inputFile != NULL );

			blockForIO_t block;
			while ( inputFile->NextWriteBlock( & block ) ) {
				if ( (size_t)outputFile->Write( block.data, block.bytes ) != block.bytes ) {
					idLib::Warning( "[%s]: Write failed.", __FUNCTION__ );
					file->error = true;
					callback->errorCode = SAVEGAME_E_INSUFFICIENT_ROOM;
					ret = -1;
					break;
				}
			}

		} else {

			if ( ( file->type & SAVEGAMEFILE_BINARY ) || ( file->type & SAVEGAMEFILE_COMPRESSED ) ) {
				if ( saveGame_checksum.GetBool() ) {
					unsigned int checksum = MD5_BlockChecksum( file->GetDataPtr(), file->Length() );
					size_t size = outputFile->WriteBig( checksum );
					if ( size != sizeof( checksum ) ) {
						idLib::Warning( "[%s]: Write failed.", __FUNCTION__ );
						file->error = true;
						callback->errorCode = SAVEGAME_E_INSUFFICIENT_ROOM;
						ret = -1;
					}
				}
			}

			size_t size = outputFile->Write( file->GetDataPtr(), file->Length() );
			if ( size != (size_t)file->Length() ) {
				idLib::Warning( "[%s]: Write failed.", __FUNCTION__ );
				file->error = true;
				callback->errorCode = SAVEGAME_E_INSUFFICIENT_ROOM;
				ret = -1;
			} else {
				idLib::PrintfIf( saveGame_verbose.GetBool(), "Saved %s (%s)\n", fileName.c_str(), outputFile->GetFullPath() );
			}
		}

		delete outputFile;

		if ( ret == SGL_ERROR_SUCCESS ) {
			// Rename the temp file over the old file (atomic on POSIX)
			if ( !fileSystem->RenameFile( tempFileName, fileName, "fs_savePath" ) ) {
				idLib::Warning( "Could not rename temporary file %s to %s.", tempFileName.c_str(), fileName.c_str() );
			}
		} else {
			fileSystem->RemoveFile( tempFileName );
			idLib::Warning( "Invalid write to temporary file %s.", tempFileName.c_str() );
		}
	}

	if ( callback->cancelled ) {
		callback->errorCode = SAVEGAME_E_CANCELLED;
	}

	return ret;
}

/*
========================
SGL_Load

Reads all files in parms->files from savegame/<parms->directory>/.
========================
*/
static int SGL_Load( idSaveLoadParms * callback ) {
	idStr saveFolder = "savegame";
	saveFolder.AppendPath( callback->directory );

	if ( fileSystem->IsFolder( saveFolder, "fs_savePath" ) != FOLDER_YES ) {
		callback->errorCode = SAVEGAME_E_FOLDER_NOT_FOUND;
		return -1;
	}

	int ret = SGL_ERROR_SUCCESS;
	for ( int i = 0; i < callback->files.Num() && ret == SGL_ERROR_SUCCESS && !callback->cancelled; i++ ) {
		idFile_SaveGame * file = callback->files[i];

		idStr filename = saveFolder;
		filename.AppendPath( file->GetName() );

		idFile * inputFile = fileSystem->OpenFileRead( filename.c_str() );
		if ( inputFile == NULL ) {
			file->error = true;
			if ( !( file->type & SAVEGAMEFILE_OPTIONAL ) ) {
				callback->errorCode = SAVEGAME_E_CORRUPTED;
				ret = -1;
			}
			continue;
		}

		if ( ( file->type & SAVEGAMEFILE_PIPELINED ) != 0 ) {

			idFile_SaveGamePipelined * outputFile = dynamic_cast< idFile_SaveGamePipelined * >( file );
			assert( outputFile != NULL );

			size_t lastReadBytes = 0;
			blockForIO_t block;
			while ( outputFile->NextReadBlock( &block, lastReadBytes ) && !callback->cancelled ) {
				lastReadBytes = inputFile->Read( block.data, block.bytes );
				if ( lastReadBytes != block.bytes ) {
					// Notify end-of-file to the save game file which will cause all reads on the
					// other end of the pipeline to return zero bytes after the pipeline is drained.
					outputFile->NextReadBlock( NULL, lastReadBytes );
					break;
				}
			}

		} else {

			size_t size = inputFile->Length();

			unsigned int originalChecksum = 0;
			if ( ( file->type & SAVEGAMEFILE_BINARY ) != 0 || ( file->type & SAVEGAMEFILE_COMPRESSED ) != 0 ) {
				if ( saveGame_checksum.GetBool() ) {
					if ( size >= sizeof( originalChecksum ) ) {
						inputFile->ReadBig( originalChecksum );
						size -= sizeof( originalChecksum );
					}
				}
			}

			file->SetLength( size );

			size_t sizeRead = inputFile->Read( (void *)file->GetDataPtr(), size );
			if ( sizeRead != size ) {
				file->error = true;
				callback->errorCode = SAVEGAME_E_CORRUPTED;
				ret = -1;
			}

			if ( ( file->type & SAVEGAMEFILE_BINARY ) != 0 || ( file->type & SAVEGAMEFILE_COMPRESSED ) != 0 ) {
				if ( saveGame_checksum.GetBool() ) {
					unsigned int checksum = MD5_BlockChecksum( file->GetDataPtr(), file->Length() );
					if ( checksum != originalChecksum ) {
						file->error = true;
						callback->errorCode = SAVEGAME_E_CORRUPTED;
						ret = -1;
					}
				}
			}
		}

		delete inputFile;
	}

	if ( callback->cancelled ) {
		callback->errorCode = SAVEGAME_E_CANCELLED;
	}

	return ret;
}

/*
========================
SGL_Delete

Deletes a complete savegame directory.
========================
*/
static int SGL_Delete( idSaveLoadParms * callback ) {
	idStr saveFolder = "savegame";
	saveFolder.AppendPath( callback->directory );

	int ret = SGL_ERROR_SUCCESS;
	if ( fileSystem->IsFolder( saveFolder, "fs_savePath" ) == FOLDER_YES ) {
		idFileList * files = fileSystem->ListFilesTree( saveFolder, "/|*" );
		for ( int i = 0; i < files->GetNumFiles() && !callback->cancelled; i++ ) {
			fileSystem->RemoveFile( files->GetFile( i ) );
		}
		fileSystem->FreeFileList( files );

		fileSystem->RemoveDir( saveFolder );
	} else {
		callback->errorCode = SAVEGAME_E_FOLDER_NOT_FOUND;
		ret = -1;
	}

	if ( callback->cancelled ) {
		callback->errorCode = SAVEGAME_E_CANCELLED;
	}

	return ret;
}

/*
========================
SGL_Enumerate

Enumerates all savegame folders under savegame/ by finding their
game.details files.
========================
*/
static int SGL_Enumerate( idSaveLoadParms * callback ) {
	idStr saveFolder = "savegame";

	callback->detailList.Clear();

	int ret = SGL_ERROR_SUCCESS;
	if ( fileSystem->IsFolder( saveFolder, "fs_savePath" ) == FOLDER_YES ) {
		idFileList * files = fileSystem->ListFilesTree( saveFolder, SAVEGAME_DETAILS_FILENAME );
		const idStrList & fileList = files->GetList();

		for ( int i = 0; i < fileList.Num() && !callback->cancelled; i++ ) {
			idSaveGameDetails * details = callback->detailList.Alloc();
			// We have more folders on disk than we have room in our save detail list, stop trying to read them in and continue with what we have
			if ( details == NULL ) {
				break;
			}
			idStr directory = fileList[i];

			idFile * file = fileSystem->OpenFileRead( directory.c_str() );

			if ( file != NULL ) {
				// Read the DETAIL file for the enumerated data
				if ( callback->mode & SAVEGAME_MBF_READ_DETAILS ) {
					if ( !SavegameReadDetailsFromFile( file, *details ) ) {
						details->damaged = true;
						ret = -1;
					}
				}

				// Use the date from the file
				struct stat st;
				if ( stat( file->GetFullPath(), &st ) == 0 ) {
					details->date = st.st_mtime;
				}
				delete file;
			} else {
				details->damaged = true;
			}

			// populate the game details struct
			directory = directory.StripFilename();
			details->slotName = directory.c_str() + saveFolder.Length() + 1; // Strip off the prefix too
		}
		fileSystem->FreeFileList( files );
	} else {
		callback->errorCode = SAVEGAME_E_FOLDER_NOT_FOUND;
		ret = -3;
	}

	if ( callback->cancelled ) {
		callback->errorCode = SAVEGAME_E_CANCELLED;
	}

	return ret;
}

/*
========================
SGL_EnumerateFiles

Enumerates all files within a particular savegame folder that match
parms->pattern / parms->postPattern.
========================
*/
static int SGL_EnumerateFiles( idSaveLoadParms * callback ) {
	idStr folder = "savegame";
	folder.AppendPath( callback->directory );

	callback->files.Clear();

	int ret = SGL_ERROR_SUCCESS;
	if ( fileSystem->IsFolder( folder, "fs_savePath" ) == FOLDER_YES ) {
		// get listing of all the files, but filter out below
		idFileList * files = fileSystem->ListFilesTree( folder, "*.*" );

		// look for the instance pattern
		for ( int i = 0; i < files->GetNumFiles() && ret == 0 && !callback->cancelled; i++ ) {
			idStr fullFilename = files->GetFile( i );
			idStr filename = fullFilename;
			filename.StripPath();

			if ( filename.IcmpPrefix( callback->pattern ) != 0 ) {
				continue;
			}
			if ( !callback->postPattern.IsEmpty() && filename.Right( callback->postPattern.Length() ).IcmpPrefix( callback->postPattern ) != 0 ) {
				continue;
			}

			// Read the DETAIL file for the enumerated data
			if ( callback->mode & SAVEGAME_MBF_READ_DETAILS ) {
				idSaveGameDetails & details = callback->description;
				idFile * uncompressed = fileSystem->OpenFileRead( fullFilename.c_str() );

				if ( uncompressed == NULL ) {
					details.damaged = true;
				} else {
					if ( !SavegameReadDetailsFromFile( uncompressed, details ) ) {
						ret = -1;
					}

					delete uncompressed;
				}

				// populate the game details struct
				details.slotName = callback->directory;
			}

			idFile_SaveGame * file = new (TAG_SAVEGAMES) idFile_SaveGame( filename, SAVEGAMEFILE_AUTO_DELETE );
			callback->files.Append( file );
		}
		fileSystem->FreeFileList( files );
	} else {
		callback->errorCode = SAVEGAME_E_FOLDER_NOT_FOUND;
		ret = -3;
	}

	if ( callback->cancelled ) {
		callback->errorCode = SAVEGAME_E_CANCELLED;
	}

	return ret;
}

/*
========================
SGL_DeleteFiles

Deletes files within a particular savegame folder that match
parms->pattern / parms->postPattern (plus any explicitly listed files).
========================
*/
static int SGL_DeleteFiles( idSaveLoadParms * callback ) {
	idStr folder = "savegame";
	folder.AppendPath( callback->directory );

	// delete the explicitly requested files first
	for ( int j = 0; j < callback->files.Num() && !callback->cancelled; ++j ) {
		idFile_SaveGame * file = callback->files[j];
		idStr fullpath = folder;
		fullpath.AppendPath( file->GetName() );
		fileSystem->RemoveFile( fullpath );
	}

	int ret = SGL_ERROR_SUCCESS;
	if ( fileSystem->IsFolder( folder, "fs_savePath" ) == FOLDER_YES ) {
		// get listing of all the files, but filter out below
		idFileList * files = fileSystem->ListFilesTree( folder, "*.*" );

		// look for the instance pattern
		for ( int i = 0; i < files->GetNumFiles() && !callback->cancelled; i++ ) {
			idStr filename = files->GetFile( i );
			filename.StripPath();

			// If there are post/pre patterns to match, make sure we adhere to the patterns
			if ( callback->pattern.IsEmpty() || ( filename.IcmpPrefix( callback->pattern ) != 0 ) ) {
				continue;
			}
			if ( callback->postPattern.IsEmpty() || ( filename.Right( callback->postPattern.Length() ).IcmpPrefix( callback->postPattern ) != 0 ) ) {
				continue;
			}

			fileSystem->RemoveFile( files->GetFile( i ) );
		}
		fileSystem->FreeFileList( files );
	} else {
		callback->errorCode = SAVEGAME_E_FOLDER_NOT_FOUND;
		ret = -3;
	}

	if ( callback->cancelled ) {
		callback->errorCode = SAVEGAME_E_CANCELLED;
	}

	return ret;
}

/*
========================
SGL_DeleteAll

Deletes all savegame directories.
========================
*/
static int SGL_DeleteAll( idSaveLoadParms * callback ) {
	idStr saveFolder = "savegame";
	int ret = SGL_ERROR_SUCCESS;

	if ( fileSystem->IsFolder( saveFolder, "fs_savePath" ) == FOLDER_YES ) {
		idFileList * files = fileSystem->ListFilesTree( saveFolder, "/|*" );
		// remove directories after files
		for ( int i = 0; i < files->GetNumFiles() && !callback->cancelled; i++ ) {
			// contained files should always be first
			if ( fileSystem->IsFolder( files->GetFile( i ), "fs_savePath" ) == FOLDER_YES ) {
				fileSystem->RemoveDir( files->GetFile( i ) );
			} else {
				fileSystem->RemoveFile( files->GetFile( i ) );
			}
		}
		fileSystem->FreeFileList( files );
	} else {
		callback->errorCode = SAVEGAME_E_FOLDER_NOT_FOUND;
		ret = -3;
	}

	if ( callback->cancelled ) {
		callback->errorCode = SAVEGAME_E_CANCELLED;
	}

	return ret;
}

/*
========================
Sys_ExecuteSavegameCommandAsyncImpl

Synchronous dispatcher: runs the requested operation inline and always raises
callbackSignal when done, so idSaveGameManager::Pump() sees the processor as
finished.
========================
*/
void Sys_ExecuteSavegameCommandAsyncImpl( idSaveLoadParms * savegameParms ) {
	assert( savegameParms != NULL );

	int ret = SGL_ERROR_SUCCESS;

	try {
		if ( savegameParms->mode & SAVEGAME_MBF_SAVE ) {
			ret = SGL_Save( savegameParms );
		} else if ( savegameParms->mode & SAVEGAME_MBF_LOAD ) {
			ret = SGL_Load( savegameParms );
		} else if ( savegameParms->mode & SAVEGAME_MBF_ENUMERATE ) {
			ret = SGL_Enumerate( savegameParms );
		} else if ( savegameParms->mode & SAVEGAME_MBF_DELETE_FOLDER ) {
			ret = SGL_Delete( savegameParms );
		} else if ( savegameParms->mode & SAVEGAME_MBF_DELETE_ALL_FOLDERS ) {
			ret = SGL_DeleteAll( savegameParms );
		} else if ( savegameParms->mode & SAVEGAME_MBF_DELETE_FILES ) {
			ret = SGL_DeleteFiles( savegameParms );
		} else if ( savegameParms->mode & SAVEGAME_MBF_ENUMERATE_FILES ) {
			ret = SGL_EnumerateFiles( savegameParms );
		}

		// if something failed and no one set an error code, do it now.
		if ( ret != 0 && savegameParms->errorCode == SAVEGAME_E_NONE ) {
			savegameParms->errorCode = SAVEGAME_E_UNKNOWN;
		}
	} catch ( ... ) {
		// if anything horrible happens, leave it up to the savegame processors to handle in PostProcess().
		savegameParms->errorCode = SAVEGAME_E_UNKNOWN;
	}

	// Make sure to cancel any save game file pipelines.
	if ( savegameParms->errorCode != SAVEGAME_E_NONE ) {
		savegameParms->CancelSaveGameFilePipelines();
	}

	// Override error if cvar set
	if ( savegame_error.GetInteger() != 0 ) {
		savegameParms->errorCode = (saveGameError_t)savegame_error.GetInteger();
	}

	// Tell the waiting caller that we are done
	savegameParms->callbackSignal.Raise();
}

/*
========================
Sys_SaveGameCheck

Synchronous check: does any savegame exist for the master local user, and is
one of them the autosave?
========================
*/
void Sys_SaveGameCheck( bool & exists, bool & autosaveExists ) {
	exists = false;
	autosaveExists = false;

	const idStr autosaveFolderStr = AddSaveFolderPrefix( SAVEGAME_AUTOSAVE_FOLDER, idSaveGameManager::PACKAGE_GAME );
	const char * autosaveFolder = autosaveFolderStr.c_str();
	const char * saveFolder = "savegame";

	if ( fileSystem->IsFolder( saveFolder, "fs_savePath" ) == FOLDER_YES ) {
		idFileList * files = fileSystem->ListFiles( saveFolder, "/" );
		const idStrList & fileList = files->GetList();

		idLib::PrintfIf( saveGame_verbose.GetBool(), "found %d savegames\n", fileList.Num() );

		for ( int i = 0; i < fileList.Num(); i++ ) {
			const char * directory = va_format( "%s/%s", saveFolder, fileList[i].c_str() );

			if ( fileSystem->IsFolder( directory, "fs_savePath" ) == FOLDER_YES ) {
				exists = true;

				idLib::PrintfIf( saveGame_verbose.GetBool(), "found savegame: %s\n", fileList[i].c_str() );

				if ( idStr::Icmp( fileList[i].c_str(), autosaveFolder ) == 0 ) {
					autosaveExists = true;
					break;
				}
			}
		}

		fileSystem->FreeFileList( files );
	}
}
