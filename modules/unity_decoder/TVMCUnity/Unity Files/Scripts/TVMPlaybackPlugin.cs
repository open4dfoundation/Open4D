using System;
using System.Runtime.InteropServices;
using UnityEngine;

public static class TVMPlaybackPlugin
{
#if UNITY_STANDALONE_WIN
    private const string LIB_NAME = "TVMDecoder";         // TVMDecoder.dll
#elif UNITY_STANDALONE_OSX
    private const string LIB_NAME = "TVMDecoder";         // libTVMDecoder.dylib
#elif UNITY_STANDALONE_LINUX
    private const string LIB_NAME = "TVMDecoder";         // libTVMDecoder.so
#elif UNITY_ANDROID
    private const string LIB_NAME = "TVMDecoder";         // libTVMDecoder.so
#else
    private const string LIB_NAME = "__Internal";         // iOS or fallback
#endif

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void DebugLogCallback(string message);

    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void RegisterUnityLogCallback(DebugLogCallback callback);

    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool InitializePlaybackManager(string path, int preLoadWindow, int decodeWindow, bool enableLogging);
    
    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void DestroyPlaybackManager();

    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool AdvanceSubSequence();

    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void LoadSubSequence(int subSequence);
    
    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void DecodeSubSequence(int subSequence);

    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FetchFrame(int frameIndex, float[] outVertices);
    
    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetCurrentDecoderTotalFrames();

    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetCurrentDecoderVertexCount();
    
        [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern int getSubSequenceCount();
    
    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetCurrentDecoderTriangleIndexCount();
    
    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void GetCurrentDecoderTriangleIndices(int[] outIndices, int maxCount);
    
    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void GetCurrentDecoderReferenceVertices(float[] outVertices);
    
    [DllImport(LIB_NAME, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool IsPlaybackManagerLoaded();
}