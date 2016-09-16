//-------------------------------------------------------------------------------------
//  BMeshFileWriter.cpp
//
//-------------------------------------------------------------------------------------

#include "stdafx.h"
#include <BinaryMeshFile.h>

#pragma warning( disable:4244 )

extern ATG::ExportScene*     g_pScene;

namespace ATG
{
    static const CHAR* strDeclUsages[] =
    {
        "POSITION",
        "BLENDWEIGHT",
        "BLENDINDICES",
        "NORMAL",
        "PSIZE",
        "TEXCOORD",
        "TANGENT",
        "BINORMAL",
        "TESSFACTOR",
        "POSITIONT",
        "COLOR",
        "FOG",
        "DEPTH",
        "SAMPLE",
    };

    std::vector<BMESH_FRAME> g_Frames;
    std::vector<BMESH_SUBSET> g_Subsets;
    std::vector<BMESH_VERTEXDATA> g_VertexDatas;
    std::vector<BMESH_INPUT_ELEMENT> g_VertexElements;
    std::vector<BMESH_MESH> g_Meshes;
    std::vector<BMESH_PARAMETER> g_Parameters;
    std::vector<BMESH_MATERIALINSTANCE> g_Materials;
    std::vector<BMESH_MATERIALMAPPING> g_MaterialMappings;
    std::vector<BMESH_ANIMATIONTRACK> g_AnimationTracks;
    std::vector<BMESH_ANIMATION> g_Animations;
    std::vector<BMESH_INFLUENCE> g_MeshInfluences;

    std::vector<ExportMaterial*> g_EncounteredMaterials;
    std::vector<ExportMeshBase*> g_EncounteredMeshes;

    struct BufferSource
    {
        const VOID* pBytes;
        UINT64 SizeBytes;
        UINT64 PositionBytes;
    };
    std::vector<BufferSource> g_VertexBufferSources;
    UINT64 g_CurrentVertexBufferSizeBytes = 0;
    std::vector<BufferSource> g_IndexBufferSources;
    UINT64 g_CurrentIndexBufferSizeBytes = 0;
    std::vector<BufferSource> g_AnimBufferSources;
    UINT64 g_CurrentAnimBufferSizeBytes = 0;

    std::vector<BufferSource> g_Strings;
    UINT64 g_CurrentStringSizeBytes = 0;

    BMESH_HEADER g_Header;

    inline UINT64 CaptureBuffer( const VOID* pBytes, UINT64 SizeBytes, BOOL Anim, BOOL IsVertex )
    {
        UINT64 CurrentPos = Anim ? g_CurrentAnimBufferSizeBytes : (IsVertex ? g_CurrentVertexBufferSizeBytes : g_CurrentIndexBufferSizeBytes);

        BufferSource BS;
        BS.pBytes = pBytes;
        BS.SizeBytes = SizeBytes;
        BS.PositionBytes = CurrentPos;

        if( Anim )
        {
            g_AnimBufferSources.push_back( BS );
            g_CurrentAnimBufferSizeBytes += BS.SizeBytes;
        }
        else if (IsVertex)
        {
            g_VertexBufferSources.push_back(BS);
            g_CurrentVertexBufferSizeBytes += BS.SizeBytes;
        }
        else
        {
            g_IndexBufferSources.push_back( BS );
            g_CurrentIndexBufferSizeBytes += BS.SizeBytes;
        }

        return CurrentPos;
    }

    inline UINT64 CaptureString( const VOID* pString, UINT64 SizeBytes )
    {
        UINT64 CurrentStringPos = g_CurrentStringSizeBytes;

        UINT StringCount = (UINT)g_Strings.size();
        for( UINT i = 0; i < StringCount; ++i )
        {
            UINT64 StringSizeBytes = g_Strings[i].SizeBytes;
            if( StringSizeBytes != SizeBytes )
            {
                continue;
            }
            if( memcmp( g_Strings[i].pBytes, pString, (size_t)SizeBytes ) == 0 )
            {
                return g_Strings[i].PositionBytes;
            }
        }

        BufferSource BS;
        VOID* pBytes = new BYTE[SizeBytes];
        memcpy( pBytes, pString, SizeBytes );
        BS.pBytes = pBytes;
        BS.SizeBytes = SizeBytes;
        BS.PositionBytes = CurrentStringPos;

        g_Strings.push_back( BS );
        g_CurrentStringSizeBytes += BS.SizeBytes;

        return CurrentStringPos;
    }

    inline VOID CaptureName( BMESH_STRING& Name, const ExportString& strName )
    {
        const CHAR* strNameAnsi = strName.SafeString();
        Name.SegmentOffsetBytes = CaptureString( strNameAnsi, strlen(strNameAnsi) + 1 );
    }

    inline VOID CaptureByteArray( BMESH_ARRAY<BYTE>& ByteArray, const VOID* pBuffer, UINT64 BufferSizeBytes, BOOL IsVertex )
    {
        ByteArray.Count = BufferSizeBytes;
        ByteArray.SegmentOffsetBytes = CaptureBuffer( pBuffer, BufferSizeBytes, FALSE, IsVertex );
    }

    inline VOID CaptureFloatArray( BMESH_ARRAY<FLOAT>& FloatArray, const VOID* pBuffer, UINT64 BufferSizeBytes )
    {
        assert( ( BufferSizeBytes & 0x3 ) == 0 );
        FloatArray.Count = BufferSizeBytes / sizeof(FLOAT);
        FloatArray.SegmentOffsetBytes = CaptureBuffer( pBuffer, BufferSizeBytes, TRUE, FALSE );
    }

    inline UINT FindSubsetIndex( ExportMeshBase* pMesh, ExportString Name )
    {
        UINT Count = pMesh->GetSubsetCount();
        for( UINT i = 0; i < Count; ++i )
        {
            if( pMesh->GetSubset(i)->GetName() == Name )
            {
                return i;
            }
        }

        assert( FALSE );
        return (UINT)-1;
    }

    UINT64 FindMaterial( ExportMaterial* pMaterial )
    {
        for( UINT i = 0; i < (UINT)g_EncounteredMaterials.size(); ++i )
        {
            if( g_EncounteredMaterials[i] == pMaterial )
            {
                return (UINT64)i;
            }
        }
        assert( FALSE );
        return (UINT)-1;
    }

    UINT64 FindMesh( ExportMeshBase* pMesh )
    {
        for( UINT i = 0; i < (UINT)g_EncounteredMeshes.size(); ++i )
        {
            if( g_EncounteredMeshes[i] == pMesh )
            {
                return (UINT64)i;
            }
        }
        assert( FALSE );
        return (UINT)-1;
    }

    VOID CaptureMaterialMapping( ExportMaterialSubsetBinding* pBinding, UINT64 MeshIndex, ExportMeshBase* pMesh )
    {
        BMESH_MATERIALMAPPING Mapping;
        ZeroMemory( &Mapping, sizeof(Mapping) );

        Mapping.Mesh.SegmentOffsetBytes = MeshIndex;
        Mapping.MeshSubsetIndex = FindSubsetIndex( pMesh, pBinding->SubsetName );
        Mapping.MaterialInstance.SegmentOffsetBytes = FindMaterial( pBinding->pMaterial );

        g_MaterialMappings.push_back( Mapping );
    }

    VOID CreateDefaultMaterial()
    {
        assert( g_Materials.size() == 0 && g_EncounteredMaterials.size() == 0 );

        BMESH_MATERIALINSTANCE Material;
        ZeroMemory( &Material, sizeof(Material) );
        CaptureName( Material.Name, "Default" );
        CaptureName( Material.MaterialName, g_pScene->Settings().strDefaultMaterialName );
        
        g_Materials.push_back( Material );
        g_EncounteredMaterials.push_back( NULL );
    }

    VOID CaptureModel( BMESH_FRAME& Frame, ExportModel* pModel )
    {
        ExportMeshBase* pMeshBase = pModel->GetMesh();
        const UINT64 MeshIndex = FindMesh( pMeshBase );

        UINT MappingCount = pModel->GetBindingCount();
        if( MappingCount == 0 )
        {
            ExportMaterialSubsetBinding DefaultBinding;
            if( g_EncounteredMaterials.size() == 0 )
            {
                CreateDefaultMaterial();
            }
            DefaultBinding.pMaterial = g_EncounteredMaterials[0];
            if( pMeshBase->GetSubsetCount() > 0 )
            {
                DefaultBinding.SubsetName = pModel->GetMesh()->GetSubset( 0 )->GetName();
                Frame.MaterialMappings.Count = 1;
                Frame.MaterialMappings.SegmentOffsetBytes = (UINT64)g_MaterialMappings.size();
                CaptureMaterialMapping( &DefaultBinding, MeshIndex, pMeshBase );
            }
            else
            {
                DefaultBinding.SubsetName = "null";
                Frame.MaterialMappings.Count = 0;
            }
        }
        else
        {
            Frame.MaterialMappings.Count = MappingCount;
            Frame.MaterialMappings.SegmentOffsetBytes = (UINT64)g_MaterialMappings.size();
            for( UINT i = 0; i < MappingCount; ++i )
            {
                CaptureMaterialMapping( pModel->GetBinding( i ), MeshIndex, pMeshBase );
            }
        }
    }

    VOID CaptureFrames( ExportFrame* pFrame, UINT ParentIndex )
    {
        UINT CurrentIndex = (UINT)g_Frames.size();
        BMESH_FRAME Frame;
        ZeroMemory( &Frame, sizeof(Frame) );
        memcpy( &Frame.Transform, &pFrame->Transform().Matrix(), sizeof(Frame.Transform) );
        Frame.pParentFrame.SegmentOffsetBytes = ParentIndex;
        CaptureName( Frame.Name, pFrame->GetName() );

        UINT ModelCount = pFrame->GetModelCount();
        if( ModelCount > 0 )
        {
            assert( ModelCount == 1 );
            CaptureModel( Frame, pFrame->GetModelByIndex( 0 ) );
        }
        else
        {
            Frame.MaterialMappings.Count = 0;
        }

        g_Frames.push_back( Frame );

        UINT ChildCount = pFrame->GetChildCount();
        for( UINT i = 0; i < ChildCount; ++i )
        {
            ExportFrame* pChild = pFrame->GetChildByIndex( i );
            CaptureFrames( pChild, CurrentIndex );
        }
    }

    UINT64 CaptureVertexData( ExportVB* pVB )
    {
        UINT64 VDIndex = g_VertexDatas.size();

        BMESH_VERTEXDATA VertexData;
        ZeroMemory( &VertexData, sizeof(VertexData) );
        VertexData.StrideBytes = pVB->GetVertexSize();
        VertexData.Count = pVB->GetVertexCount();
        CaptureByteArray( VertexData.ByteBuffer, pVB->GetVertexData(), pVB->GetVertexDataSize(), TRUE );

        g_VertexDatas.push_back( VertexData );

        return VDIndex;
    }

    inline VOID CaptureSubset( const ExportIBSubset* pSubset, UINT VertexCount )
    {
        BMESH_SUBSET Subset;
        ZeroMemory( &Subset, sizeof(Subset) );

        CaptureName( Subset.Name, pSubset->GetName() );

        switch( pSubset->GetPrimitiveType() )
        {
        case ExportIBSubset::TriangleList:
            Subset.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
        case ExportIBSubset::TriangleStrip:
            Subset.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            break;
        case ExportIBSubset::QuadList:
        default:
            ExportLog::LogError( "Invalid subset primitive topology!" );
            assert( FALSE );
            break;
        }

        Subset.StartIndex = pSubset->GetStartIndex();
        Subset.IndexCount = pSubset->GetIndexCount();
        Subset.VertexCount = VertexCount;

        g_Subsets.push_back( Subset );
    }

    VOID ConvertElement( BMESH_INPUT_ELEMENT& ElementDesc, const D3DVERTEXELEMENT9& LegacyDesc )
    {
        ZeroMemory( &ElementDesc, sizeof(ElementDesc) );
        ElementDesc.AlignedByteOffset = LegacyDesc.Offset;
        ElementDesc.SemanticIndex = LegacyDesc.UsageIndex;
        ElementDesc.InputSlot = LegacyDesc.Stream;

        static const DXGI_FORMAT D3DDeclTypeToDXGIFormat[] =
        {
            // D3DDECLTYPE_FLOAT1    =  0,  // 1D float expanded to (value, 0., 0., 1.)
            DXGI_FORMAT_R32_FLOAT,
            // D3DDECLTYPE_FLOAT2    =  1,  // 2D float expanded to (value, value, 0., 1.)
            DXGI_FORMAT_R32G32_FLOAT,
            // D3DDECLTYPE_FLOAT3    =  2,  // 3D float expanded to (value, value, value, 1.)
            DXGI_FORMAT_R32G32B32_FLOAT,
            // D3DDECLTYPE_FLOAT4    =  3,  // 4D float
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            // D3DDECLTYPE_D3DCOLOR  =  4,  // 4D packed unsigned bytes mapped to 0. to 1. range
            DXGI_FORMAT_R8G8B8A8_UNORM,
            // D3DDECLTYPE_UBYTE4    =  5,  // 4D unsigned byte
            DXGI_FORMAT_R8G8B8A8_UINT,
            // D3DDECLTYPE_SHORT2    =  6,  // 2D signed short expanded to (value, value, 0., 1.)
            DXGI_FORMAT_R16G16_SINT,
            // D3DDECLTYPE_SHORT4    =  7,  // 4D signed short
            DXGI_FORMAT_R16G16B16A16_SINT,
            // D3DDECLTYPE_UBYTE4N   =  8,  // Each of 4 bytes is normalized by dividing to 255.0
            DXGI_FORMAT_R8G8B8A8_UNORM,
            // D3DDECLTYPE_SHORT2N   =  9,  // 2D signed short normalized (v[0]/32767.0,v[1]/32767.0,0,1)
            DXGI_FORMAT_R16G16_SNORM,
            // D3DDECLTYPE_SHORT4N   = 10,  // 4D signed short normalized (v[0]/32767.0,v[1]/32767.0,v[2]/32767.0,v[3]/32767.0)
            DXGI_FORMAT_R16G16B16A16_SNORM,
            // D3DDECLTYPE_USHORT2N  = 11,  // 2D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,0,1)
            DXGI_FORMAT_R16G16_UNORM,
            // D3DDECLTYPE_USHORT4N  = 12,  // 4D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,v[2]/65535.0,v[3]/65535.0)
            DXGI_FORMAT_R16G16B16A16_UNORM,
            // D3DDECLTYPE_UDEC3     = 13,  // 3D unsigned 10 10 10 format expanded to (value, value, value, 1)
            DXGI_FORMAT_R10G10B10A2_UINT,
            // D3DDECLTYPE_DEC3N     = 14,  // 3D signed 10 10 10 format normalized and expanded to (v[0]/511.0, v[1]/511.0, v[2]/511.0, 1)
            DXGI_FORMAT_R10G10B10A2_UNORM,
            // D3DDECLTYPE_FLOAT16_2 = 15,  // Two 16-bit floating point values, expanded to (value, value, 0, 1)
            DXGI_FORMAT_R16G16_FLOAT,
            // D3DDECLTYPE_FLOAT16_4 = 16,  // Four 16-bit floating point values
            DXGI_FORMAT_R16G16B16A16_FLOAT,
        };
        assert( LegacyDesc.Type < ARRAYSIZE(D3DDeclTypeToDXGIFormat) );
        ElementDesc.Format = D3DDeclTypeToDXGIFormat[ LegacyDesc.Type ];

        assert( LegacyDesc.Usage < ARRAYSIZE(strDeclUsages) );
        const CHAR* strSemanticName = strDeclUsages[LegacyDesc.Usage];
        ElementDesc.SemanticName.SegmentOffsetBytes = CaptureString( strSemanticName, strlen(strSemanticName) + 1 );
    }

    VOID CaptureVertexElements( BMESH_MESH& Mesh, ExportMesh* pPolyMesh )
    {
        const UINT ElementCount = pPolyMesh->GetVertexDeclElementCount();

        if( ElementCount == 0 )
        {
            Mesh.VertexElements.Count = 0;
            Mesh.VertexElements.SegmentOffsetBytes = 0;
            return;
        }

        Mesh.VertexElements.Count = ElementCount;
        Mesh.VertexElements.SegmentOffsetBytes = g_VertexElements.size();

        // TODO: coalesce element descs

        for( UINT i = 0; i < ElementCount; ++i )
        {
            BMESH_INPUT_ELEMENT ElementDesc;
            ConvertElement( ElementDesc, pPolyMesh->GetVertexDeclElement( i ) );
            g_VertexElements.push_back( ElementDesc );
        }
    }

    VOID CaptureMesh( ExportMeshBase* pMesh )
    {
        if( pMesh->GetMeshType() != ExportMeshBase::PolyMesh )
        {
            return;
        }

        ExportMesh* pPolyMesh = reinterpret_cast<ExportMesh*>( pMesh );

        BMESH_MESH Mesh;
        ZeroMemory( &Mesh, sizeof(Mesh) );
        CaptureName( Mesh.Name, pPolyMesh->GetName() );

        CaptureVertexElements( Mesh, pPolyMesh );

        if( pPolyMesh->GetIB() != NULL )
        {
            ExportIB* pIB = pPolyMesh->GetIB();
            CaptureByteArray( Mesh.IndexData.ByteBuffer, pIB->GetIndexData(), pIB->GetIndexDataSize(), FALSE );
            Mesh.IndexData.Format = pIB->GetIndexSize() == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            Mesh.IndexData.Count = pIB->GetIndexCount();
        }

        UINT VertexCount = 0;
        if( pPolyMesh->GetVB() != NULL )
        {
            ExportVB* pVB = pPolyMesh->GetVB();
            Mesh.VertexDatas.Count = 1;
            Mesh.VertexDatas.SegmentOffsetBytes = CaptureVertexData( pVB );
            VertexCount = pVB->GetVertexCount();
        }

        UINT SubsetCount = pPolyMesh->GetSubsetCount();
        Mesh.Subsets.Count = SubsetCount;
        Mesh.Subsets.SegmentOffsetBytes = g_Subsets.size();
        for( UINT i = 0; i < SubsetCount; ++i )
        {
            CaptureSubset( pPolyMesh->GetSubset(i), VertexCount );
        }

        UINT InfluenceCount = pPolyMesh->GetInfluenceCount();
        Mesh.Influences.SegmentOffsetBytes = g_MeshInfluences.size();
        Mesh.Influences.Count = InfluenceCount;
        for( UINT i = 0; i < InfluenceCount; ++i )
        {
            BMESH_INFLUENCE Inf = {};
            CaptureName( Inf.Name, pPolyMesh->GetInfluence( i ) );
            g_MeshInfluences.push_back( Inf );
        }

        g_Meshes.push_back( Mesh );
        g_EncounteredMeshes.push_back( pMesh );
    }

    UINT MatchSemantic( const ExportMaterialParameter& Param )
    {
        UINT Semantic = BMESH_SEM_NONE;

        switch( Param.ParamType )
        {
        case MPT_TEXTURE2D:
        case MPT_TEXTURECUBE:
        case MPT_TEXTUREVOLUME:
            Semantic = BMESH_SEM_TEXTURE;
            break;
        }

        static const ExportString s_DiffuseMapName( "DiffuseTexture" );
        static const ExportString s_NormalMapName( "NormalMapTexture" );
        static const ExportString s_SpecularMapName( "SpecularMapTexture" );

        if( Param.Name == s_DiffuseMapName )
        {
            Semantic = BMESH_SEM_TEXTURE_DIFFUSEMAP;
        }
        else if( Param.Name == s_NormalMapName )
        {
            Semantic = BMESH_SEM_TEXTURE_NORMALMAP;
        }
        else if( Param.Name == s_SpecularMapName )
        {
            Semantic = BMESH_SEM_TEXTURE_SPECULARMAP;
        }

        return Semantic;
    }

    VOID CaptureParameter( const ExportMaterialParameter& Param )
    {
        BMESH_PARAMETER MaterialParam;
        ZeroMemory( &MaterialParam, sizeof(MaterialParam) );

        CaptureName( MaterialParam.Name, Param.Name );

        switch( Param.ParamType )
        {
        case MPT_TEXTURE2D:
        case MPT_TEXTURECUBE:
        case MPT_TEXTUREVOLUME:
        case MPT_STRING:
            MaterialParam.Type = BMESH_PARAM_STRING;
            CaptureName( MaterialParam.StringData, Param.ValueString );
            break;
        case MPT_FLOAT:
            MaterialParam.Type = BMESH_PARAM_FLOAT;
            MaterialParam.FloatData[0] = Param.ValueFloat[0];
            break;
        case MPT_FLOAT2:
            MaterialParam.Type = BMESH_PARAM_FLOAT2;
            MaterialParam.FloatData[0] = Param.ValueFloat[0];
            MaterialParam.FloatData[1] = Param.ValueFloat[1];
            break;
        case MPT_FLOAT3:
            MaterialParam.Type = BMESH_PARAM_FLOAT3;
            MaterialParam.FloatData[0] = Param.ValueFloat[0];
            MaterialParam.FloatData[1] = Param.ValueFloat[1];
            MaterialParam.FloatData[2] = Param.ValueFloat[2];
            break;
        case MPT_FLOAT4:
            MaterialParam.Type = BMESH_PARAM_FLOAT4;
            MaterialParam.FloatData[0] = Param.ValueFloat[0];
            MaterialParam.FloatData[1] = Param.ValueFloat[1];
            MaterialParam.FloatData[2] = Param.ValueFloat[2];
            MaterialParam.FloatData[3] = Param.ValueFloat[3];
            break;
        case MPT_BOOL:
            MaterialParam.Type = BMESH_PARAM_BOOLEAN;
            MaterialParam.BooleanData = Param.ValueInt;
            break;
        case MPT_INTEGER:
            MaterialParam.Type = BMESH_PARAM_INTEGER;
            MaterialParam.IntegerData = Param.ValueInt;
            break;
        }

        MaterialParam.Semantic = MatchSemantic( Param );

        g_Parameters.push_back( MaterialParam );
    }

    VOID CaptureMaterial( ExportMaterial* pMaterial )
    {
        BMESH_MATERIALINSTANCE Material;
        ZeroMemory( &Material, sizeof(Material) );
        CaptureName( Material.Name, pMaterial->GetName() );
        CaptureName( Material.MaterialName, pMaterial->GetDefaultMaterialName() );

        UINT ParamCount = 0;
        UINT ParamOffset = (UINT)g_Parameters.size();
        const MaterialParameterList* pParamList = pMaterial->GetParameterList();
        if( pParamList != NULL )
        {
            MaterialParameterList::const_iterator iter = pParamList->cbegin();
            MaterialParameterList::const_iterator end = pParamList->cend();

            while( iter != end )
            {
                const ExportMaterialParameter& Param = *iter;

                CaptureParameter( Param );
                ++ParamCount;

                ++iter;
            }
        }

        if (pMaterial->IsTransparent())
        {
            BMESH_PARAMETER Param = {};
            ExportString ParamName("Transparent");
            CaptureName(Param.Name, ParamName);
            Param.Type = BMESH_PARAM_BOOLEAN;
            Param.BooleanData = TRUE;
            Param.Semantic = BMESH_SEM_MATERIAL_TRANSPARENT;
            g_Parameters.push_back(Param);
            ++ParamCount;
        }

        Material.Parameters.Count = ParamCount;
        Material.Parameters.SegmentOffsetBytes = ParamOffset;

        g_Materials.push_back( Material );
        g_EncounteredMaterials.push_back( pMaterial );
    }

    UINT64 CaptureAnimationTrack( const ExportAnimationTrack* pTrack )
    {
        UINT64 Index = g_AnimationTracks.size();

        BMESH_ANIMATIONTRACK Track = {};
        CaptureName( Track.Name, pTrack->GetName() );

        Track.PositionKeyCount = pTrack->TransformTrack.GetPositionKeyCount();
        CaptureFloatArray( Track.PositionKeys, pTrack->TransformTrack.GetPositionData(), pTrack->TransformTrack.GetPositionDataSize() );

        Track.OrientationKeyCount = pTrack->TransformTrack.GetOrientationKeyCount();
        CaptureFloatArray( Track.OrientationKeys, pTrack->TransformTrack.GetOrientationData(), pTrack->TransformTrack.GetOrientationDataSize() );

        Track.ScaleKeyCount = pTrack->TransformTrack.GetScaleKeyCount();
        CaptureFloatArray( Track.ScaleKeys, pTrack->TransformTrack.GetScaleData(), pTrack->TransformTrack.GetScaleDataSize() );

        g_AnimationTracks.push_back( Track );

        return Index;
    }

    VOID CaptureAnimation( ExportAnimation* pAnimation )
    {
		if (pAnimation->ShouldSkipExport())
		{
			return;
		}

        BMESH_ANIMATION Anim = {};
        CaptureName( Anim.Name, pAnimation->GetName() );

        Anim.Tracks.Count = pAnimation->GetTrackCount();
        Anim.Tracks.SegmentOffsetBytes = g_AnimationTracks.size();
        Anim.DurationSeconds = pAnimation->GetDuration();
        for( UINT i = 0; i < Anim.Tracks.Count; ++i )
        {
            CaptureAnimationTrack( pAnimation->GetTrack( i ) );
        }

        g_Animations.push_back( Anim );
    }

    BOOL WriteBMeshFile( const CHAR* strFileName, ExportManifest* pManifest )
    {
        HANDLE hFile = CreateFileA( strFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
        if( hFile == INVALID_HANDLE_VALUE )
        {
            ExportLog::LogError( "Failed to write bmesh file \"%s\".", strFileName );
            return FALSE;
        }

        ExportLog::LogMsg( 1, "Writing bmesh file \"%s\".", strFileName );

        g_Frames.clear();
        g_Subsets.clear();
        g_VertexDatas.clear();
        g_VertexElements.clear();
        g_Meshes.clear();
        g_Parameters.clear();
        g_Materials.clear();
        g_MaterialMappings.clear();
        g_Animations.clear();
        g_AnimationTracks.clear();
        g_MeshInfluences.clear();

        g_EncounteredMaterials.clear();
        g_EncounteredMeshes.clear();

        g_VertexBufferSources.clear();
        g_CurrentVertexBufferSizeBytes = 0;
        g_IndexBufferSources.clear();
        g_CurrentIndexBufferSizeBytes = 0;
        g_AnimBufferSources.clear();
        g_CurrentAnimBufferSizeBytes = 0;

        g_Strings.clear();
        g_CurrentStringSizeBytes = 0;

        ZeroMemory( &g_Header, sizeof(g_Header) );
        g_Header.Magic = BMESH_MAGIC;
        g_Header.Version = BMESH_VERSION;
        g_Header.HeaderSizeBytes = sizeof(BMESH_HEADER);

        for( UINT i = 0; i < g_pScene->GetMeshCount(); ++i )
        {
            CaptureMesh( g_pScene->GetMesh( i ) );
        }
        for( UINT i = 0; i < g_pScene->GetMaterialCount(); ++i )
        {
            CaptureMaterial( g_pScene->GetMaterial( i ) );
        }
        CaptureFrames( g_pScene, 0 );
        for( UINT i = 0; i < g_pScene->GetAnimationCount(); ++i )
        {
            CaptureAnimation( g_pScene->GetAnimation( i ) );
        }

        for (UINT32 i = 0; i < g_Meshes.size(); ++i)
        {
            BMESH_MESH& mesh = g_Meshes[i];
            mesh.IndexData.ByteBuffer.SegmentOffsetBytes += g_CurrentVertexBufferSizeBytes;
        }

        UINT64 FrameSizeBytes = g_Frames.size() * sizeof(BMESH_FRAME);
        UINT64 SubsetSizeBytes = g_Subsets.size() * sizeof(BMESH_SUBSET);
        UINT64 VertexDataSizeBytes = g_VertexDatas.size() * sizeof(BMESH_VERTEXDATA);
        UINT64 VertexElementSizeBytes = g_VertexElements.size() * sizeof(BMESH_INPUT_ELEMENT);
        UINT64 MeshSizeBytes = g_Meshes.size() * sizeof(BMESH_MESH);
        UINT64 ParameterSizeBytes = g_Parameters.size() * sizeof(BMESH_PARAMETER);
        UINT64 MaterialSizeBytes = g_Materials.size() * sizeof(BMESH_MATERIALINSTANCE);
        UINT64 MappingSizeBytes = g_MaterialMappings.size() * sizeof(BMESH_MATERIALMAPPING);
        UINT64 AnimTrackSizeBytes = g_AnimationTracks.size() * sizeof(BMESH_ANIMATIONTRACK);
        UINT64 AnimSizeBytes = g_Animations.size() * sizeof(BMESH_ANIMATION);
        UINT64 InfluencesSizeBytes = g_MeshInfluences.size() * sizeof(BMESH_INFLUENCE);

        UINT64 DataSegmentSizeBytes = FrameSizeBytes + SubsetSizeBytes + VertexDataSizeBytes + VertexElementSizeBytes + MeshSizeBytes + ParameterSizeBytes + MaterialSizeBytes + MappingSizeBytes + AnimTrackSizeBytes + AnimSizeBytes + InfluencesSizeBytes;

        g_Header.DataSegmentSizeBytes = DataSegmentSizeBytes;
        g_Header.StringsSizeBytes = g_CurrentStringSizeBytes;
        g_Header.MeshBufferSegmentSizeBytes = g_CurrentVertexBufferSizeBytes + g_CurrentIndexBufferSizeBytes;
        g_Header.AnimBufferSegmentSizeBytes = g_CurrentAnimBufferSizeBytes;

        UINT64 DataSegmentOffsetBytes = 0;

        g_Header.Frames.Count = g_Frames.size();
        g_Header.Frames.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += FrameSizeBytes;

        g_Header.Subsets.Count = g_Subsets.size();
        g_Header.Subsets.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += SubsetSizeBytes;

        g_Header.VertexDatas.Count = g_VertexDatas.size();
        g_Header.VertexDatas.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += VertexDataSizeBytes;

        g_Header.VertexElements.Count = g_VertexElements.size();
        g_Header.VertexElements.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += VertexElementSizeBytes;

        g_Header.Meshes.Count = g_Meshes.size();
        g_Header.Meshes.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += MeshSizeBytes;

        g_Header.MaterialParameters.Count = g_Parameters.size();
        g_Header.MaterialParameters.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += ParameterSizeBytes;

        g_Header.MaterialInstances.Count = g_Materials.size();
        g_Header.MaterialInstances.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += MaterialSizeBytes;

        g_Header.MaterialMappings.Count = g_MaterialMappings.size();
        g_Header.MaterialMappings.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += MappingSizeBytes;

        g_Header.AnimationTracks.Count = g_AnimationTracks.size();
        g_Header.AnimationTracks.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += AnimTrackSizeBytes;

        g_Header.Animations.Count = g_Animations.size();
        g_Header.Animations.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += AnimSizeBytes;

        g_Header.Influences.Count = g_MeshInfluences.size();
        g_Header.Influences.SegmentOffsetBytes = DataSegmentOffsetBytes;
        DataSegmentOffsetBytes += InfluencesSizeBytes;

        DWORD BytesWritten = 0;
        WriteFile( hFile, &g_Header, g_Header.HeaderSizeBytes, &BytesWritten, NULL );

        if( FrameSizeBytes > 0 )
        {
            WriteFile( hFile, &g_Frames.front(), FrameSizeBytes, &BytesWritten, NULL );
        }
        if( SubsetSizeBytes > 0 )
        {
            WriteFile( hFile, &g_Subsets.front(), SubsetSizeBytes, &BytesWritten, NULL );
        }
        if( VertexDataSizeBytes > 0 )
        {
            WriteFile( hFile, &g_VertexDatas.front(), VertexDataSizeBytes, &BytesWritten, NULL );
        }
        if( VertexElementSizeBytes > 0 )
        {
            WriteFile( hFile, &g_VertexElements.front(), VertexElementSizeBytes, &BytesWritten, NULL );
        }
        if( MeshSizeBytes > 0 )
        {
            WriteFile( hFile, &g_Meshes.front(), MeshSizeBytes, &BytesWritten, NULL );
        }
        if( ParameterSizeBytes > 0 )
        {
            WriteFile( hFile, &g_Parameters.front(), ParameterSizeBytes, &BytesWritten, NULL );
        }
        if( MaterialSizeBytes > 0 )
        {
            WriteFile( hFile, &g_Materials.front(), MaterialSizeBytes, &BytesWritten, NULL );
        }
        if( MappingSizeBytes > 0 )
        {
            WriteFile( hFile, &g_MaterialMappings.front(), MappingSizeBytes, &BytesWritten, NULL );
        }
        if( AnimTrackSizeBytes > 0 )
        {
            WriteFile( hFile, &g_AnimationTracks.front(), AnimTrackSizeBytes, &BytesWritten, NULL );
        }
        if( AnimSizeBytes > 0 )
        {
            WriteFile( hFile, &g_Animations.front(), AnimSizeBytes, &BytesWritten, NULL );
        }
        if( InfluencesSizeBytes > 0 )
        {
            WriteFile( hFile, &g_MeshInfluences.front(), InfluencesSizeBytes, &BytesWritten, NULL );
        }

        UINT StringCount = (UINT)g_Strings.size();
        for( UINT i = 0; i < StringCount; ++i )
        {
            WriteFile( hFile, g_Strings[i].pBytes, g_Strings[i].SizeBytes, &BytesWritten, NULL );
            delete[] g_Strings[i].pBytes;
        }
        g_Strings.clear();
        g_CurrentStringSizeBytes = 0;

        UINT BufferCount = (UINT)g_AnimBufferSources.size();
        for( UINT i = 0; i < BufferCount; ++i )
        {
            WriteFile( hFile, g_AnimBufferSources[i].pBytes, g_AnimBufferSources[i].SizeBytes, &BytesWritten, NULL );
        }

        BufferCount = (UINT)g_VertexBufferSources.size();
        for( UINT i = 0; i < BufferCount; ++i )
        {
            WriteFile( hFile, g_VertexBufferSources[i].pBytes, g_VertexBufferSources[i].SizeBytes, &BytesWritten, NULL );
        }

        BufferCount = (UINT)g_IndexBufferSources.size();
        for (UINT i = 0; i < BufferCount; ++i)
        {
            WriteFile(hFile, g_IndexBufferSources[i].pBytes, g_IndexBufferSources[i].SizeBytes, &BytesWritten, NULL);
        }

        CloseHandle( hFile );

        return TRUE;
    }
}
