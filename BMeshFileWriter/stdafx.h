//-------------------------------------------------------------------------------------
//  stdafx.h
//
//  Precompiled header for the BMeshFileWriter project.
//
//-------------------------------------------------------------------------------------

#pragma once

// #ifndef _SECURE_SCL
// #define _SECURE_SCL 0
// #endif
// 
// #ifndef _ITERATOR_DEBUG_LEVEL
// #define _ITERATOR_DEBUG_LEVEL 0
// #endif

#pragma warning( disable: 4100 )

#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS 1

#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <WindowsX.h>
#include <list>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <shellapi.h>
#include <d3d11.h>

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <DirectXPackedVector.h>

#include "..\ExportObjects\ExportXmlParser.h"
#include "..\ExportObjects\ExportPath.h"
#include "..\ExportObjects\ExportMaterial.h"
#include "..\ExportObjects\ExportObjects.h"

#pragma warning (disable:4267)