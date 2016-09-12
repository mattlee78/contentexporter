//-------------------------------------------------------------------------------------
//  BMeshFileWriter.h
//
//  Entry point for writing BMESH files.  This file writer takes data from the
//  ExportScene stored in a global variable (g_pScene).
//
//-------------------------------------------------------------------------------------

#pragma once

namespace ATG
{

class ExportManifest;

BOOL WriteBMeshFile( const CHAR* strFileName, ExportManifest* pManifest );

}
