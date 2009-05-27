//#undef _WIN32
/*  #########################################################################  */

#include "StdAfx.h"

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <errno.h>
#ifdef _WIN32
#include <tchar.h>
#else
//#define _tprintf printf
//#define _stprintf sprintf
//#define _tsystem system
#endif

#include "../cpp/Common/MyString.h"
#include "../cpp/Common/IntToString.h"
#include "../cpp/Common/StringConvert.h"
#include "../cpp/Common/Wildcard.h"
#include "../cpp/Common/CommandLineParser.h"
#include "../cpp/Common/ListFileUtils.h"
#include "../cpp/Windows/FileFind.h"
#include "../cpp/Windows/FileDir.h"
#include "../cpp/Windows/FileIO.h"
#include "../cpp/Windows/DLL.h"
#include "../cpp/7zip/Archive/7z/7zHandler.h"
#include "../cpp/7zip/UI/Common/ExitCode.h"
#include "../cpp/7zip/MyVersion.h"

extern "C"
{
    #include "../c/7zCrc.h"
}

#include "t7z.h"

/*  #########################################################################  */

#ifdef _WIN32
#if !defined(UNICODE) || !defined(_UNICODE)
    #error you're trying to compile t7z without unicode support, do not do this
    //if you do, be sure to change signature, as it will produce different results in certain cases
#endif
#endif

/*  #########################################################################  */

static const char *k7zCopyrightString = "7-Zip"
#ifndef EXTERNAL_CODECS
" (A)"
#endif

#ifdef _WIN64
" [64]"
#endif

" " MY_VERSION_COPYRIGHT_DATE;

extern int MY_CDECL main3
(
#ifndef _WIN32
    int numArguments,const char*arguments[]
#endif
);
extern bool g_CaseSensitive;

/*  #########################################################################  */

namespace torrent7z
{

/*  #########################################################################  */

struct finfo
{
    NWindows::NFile::NFind::CFileInfo fileInfo;
    UInt32 fcount;
    UInt32 dcount;
    UInt32 tcount;
    UInt64 ttl_fs;
    UInt64 max_fs;
    UInt64 min_fs;
    UInt64 avg_fs;
    bool debugprint;
    CSysStringVector*dirlist;
    CSysStringVector*filelist;
};

struct cfinfo
{
    UInt32 fcountt;
    UInt32 fcount;
    UInt32 fcountp;
    UInt32 fcounte;
    UInt32 fcountr;
};

typedef int (*file_proc)(const NWindows::NFile::NFind::CFileInfo&fileInfo,const CSysString&,const CSysString&,void*);

#define fix(x,a) ((a)*(((x)+(a)-1)/(a)))

const int crcsz=128;

bool g_stripFileNames;
bool g_singleFile;
bool g_forceRecompress;
bool g_batch;
bool g_IsParentGui;
bool g_firstInstance;
bool g_defaultPriority;

UINT codePage;
CSysString logFileName;
CSysString t7z_exe,e7z_exe;
char*buffer;

#ifdef _WIN32
const TCHAR dirDelimiter0='\\';
const TCHAR dirDelimiter1='/';
#else
const TCHAR dirDelimiter0='/';
const TCHAR dirDelimiter1='/';
#endif

#ifdef DEBUG
const bool debug=true;
#else
const bool debug=false;
#endif

/*  #########################################################################  */

static inline UINT GetCurrentCodePage()
{
#ifdef _WIN32
    return ::AreFileApisANSI()?CP_ACP:CP_OEMCP;
#else
    return CP_OEMCP;
#endif
}

/*  #########################################################################  */

#ifdef _UNICODE
#define text(x) TEXT(x)
#define u2a(a) (a)
#define a2u(u) (u)
#else
#define text(x) (x)
AString u2a(const UString&u)
{
    bool tmp;
    return UnicodeStringToMultiByte(u,GetCurrentCodePage(),'?',tmp);
}

UString a2u(const AString&a)
{
    return MultiByteToUnicodeString(a,GetCurrentCodePage());
}
#endif

/*  #########################################################################  */

#ifdef _WIN32
    #define setenv(a,b,c) SetEnvironmentVariable(a,b)
#endif

/*  #########################################################################  */

bool stripFileNames()
{
    return g_stripFileNames;
}

/*  #########################################################################  */

const char*get_t7zsig()
{
    static char _t7zsig[t7zsig_size];
    memcpy(_t7zsig,t7zsig,t7zsig_size);
#ifdef _UNICODE
    _t7zsig[16]=1;
#else
    _t7zsig[16]=0;
#endif
    _t7zsig[16]|=g_singleFile?2:0;
    _t7zsig[16]|=g_stripFileNames?4:0;
    return _t7zsig;
}

/*  #########################################################################  */

bool compare_t7zsig(const char*fdata)
{
    char _t7zsig[t7zsig_size];
    memcpy(_t7zsig,fdata,t7zsig_size);
    if(_t7zsig[16]&(~7))
    {
        return 0;
    }
#ifdef _UNICODE
    if(!(_t7zsig[16]&1))
#else
    if(_t7zsig[16]&1)
#endif
    {
        return 0;
    }
    if(_t7zsig[16]&2)
    {
        bool a=g_stripFileNames;
        bool b=(_t7zsig[16]&4)!=0;
        if(a!=b)
        {
            return 0;
        }
    }
    else
    {
        if(_t7zsig[16]&4)
        {
            return 0;
        }
    }
    _t7zsig[16]=0;
    return memcmp(_t7zsig,t7zsig,t7zsig_size)==0;
}

/*  #########################################################################  */

TCHAR*_zt(const CSysString&str)
{
    const TCHAR*zt=str;
    return const_cast<TCHAR*>(zt);
}

/*  #########################################################################  */

int strcmpi_x(const UString&str0,const UString&str1)
{
    UString str=str0.Left(str1.Length());
    return str.CompareNoCase(str1);
}

/*  #########################################################################  */

int is_command(const UString&str)
{
    if(str.CompareNoCase(L"a")==0)return 1;
    if(str.CompareNoCase(L"b")==0)return 1;
    if(str.CompareNoCase(L"d")==0)return 1;
    if(str.CompareNoCase(L"e")==0)return 1;
    if(str.CompareNoCase(L"l")==0)return 1;
    if(str.CompareNoCase(L"t")==0)return 1;
    if(str.CompareNoCase(L"u")==0)return 1;
    if(str.CompareNoCase(L"x")==0)return 1;
    return 0;
}

/*  #########################################################################  */

int is_switch(const UString&str)
{
    if(str[0]=='-')return 1;
    return 0;
}

/*  #########################################################################  */

int is_allowed(const UString&str)
{
    if(str.CompareNoCase(L"e")==0)return 1;
    if(str.CompareNoCase(L"l")==0)return 1;
    if(str.CompareNoCase(L"t")==0)return 1;
    if(str.CompareNoCase(L"x")==0)return 1;

    if(str.CompareNoCase(L"--")==0)return 1;
    if(str.CompareNoCase(L"-bd")==0)return 1;
    if(    strcmpi_x(str,L"-ao")==0)return 1;
    if(    strcmpi_x(str,L"-o")==0)return 1;
    if(str.CompareNoCase(L"-slt")==0)return 1;
    if(str.CompareNoCase(L"-y")==0)return 1;
    if(str.CompareNoCase(L"-ssc-")==0)
    {
        g_CaseSensitive=0;
        return 1;
    }
    if(str.CompareNoCase(L"-ssc")==0)
    {
        g_CaseSensitive=1;
        return 1;
    }
    if(str.CompareNoCase(L"-scsutf-8")==0)
    {
        codePage=CP_UTF8;
        return 1;
    }
    if(str.CompareNoCase(L"-scswin")==0)
    {
        codePage=CP_ACP;
        return 1;
    }
    if(str.CompareNoCase(L"-scsdos")==0)
    {
        codePage=CP_OEMCP;
        return 1;
    }
    return 0;
}

/*  #########################################################################  */

CSysString Int64ToString(Int64 a,int pad=0)
{
    CSysString dstr(text("012345678901234567890123456789"));
    const TCHAR*ps=_zt(dstr);
    ConvertInt64ToString(a,const_cast<TCHAR*>(ps));
    CSysString res(ps);
    while(res.Length()<pad)
    {
        res=text(" ")+res;
    }
    return res;
}

/*  #########################################################################  */

CSysString combine_path(const CSysString&path0,const CSysString&path1)
{
    return (path0.Compare(text(""))==0)?path1:((path1.Compare(text(""))==0)?path0:(path0+CSysString(dirDelimiter0)+path1));
}

/*  #########################################################################  */

CSysString clean_path(const CSysString&path)
{
    CSysString path_t(path);
#ifdef _WIN32
    while(path_t.Replace(text("/"),dirDelimiter0));
#endif
    if(path_t[path_t.Length()-1]==dirDelimiter0||path_t[path_t.Length()-1]==dirDelimiter1)
    {
        path_t=path_t.Left(path_t.Length()-1);
    }
    return path_t;
}

/*  #########################################################################  */

int is_path_abs(const CSysString&path)
{
    return path.Find(text(".")+CSysString(dirDelimiter0))>=0||path.Find(text(".")+CSysString(dirDelimiter1))>=0||/*path.Find(text("..")+CSysString(dirDelimiter0))>=0||path.Find(text("..")+CSysString(dirDelimiter1))>=0||*/path.Find(text(":"))>=0||path[0]==dirDelimiter0||path[0]==dirDelimiter1;
}

/*  #########################################################################  */

int file_exists(const CSysString&fname)
{
    if(!DoesNameContainWildCard(a2u(fname)))
    {
        NWindows::NFile::NFind::CEnumerator match(fname);
        NWindows::NFile::NFind::CFileInfo fileInfo;
        if(match.Next(fileInfo))
        {
            return fileInfo.IsDir()?2:1;
        }
    }
    return 0;
}

/*  #########################################################################  */

void log(const CSysString&str,int force=0)
{
    static CSysString*plogdata=0;
    if(plogdata==0)
    {
        plogdata=new CSysString;
    }
    CSysString&logdata=plogdata[0];
    logdata+=str;
    if(logFileName.Compare(text(""))==0)
    {
        return;
    }
    if(logdata.Length()<1024&&force==0)
    {
        return;
    }
    if(logdata.Compare(text(""))==0)
    {
        return;
    }
    int ew=0;
    UInt32 ar;
    UInt64 foffs;
    if(file_exists(logFileName)==0)
    {
        NWindows::NFile::NIO::COutFile fwrite;
        if(fwrite.Open(logFileName,CREATE_NEW))
        {
#ifdef _UNICODE
            fwrite.Write("\xFF\xFE",2,ar);
            if(ar!=2)
            {
                ew=1;
            }
#endif
            fwrite.Close();
        }
        else
        {
            ew=1;
        }
    }
    NWindows::NFile::NIO::COutFile fwrite;
    if(fwrite.Open(logFileName,OPEN_EXISTING))
    {
        fwrite.SeekToEnd(foffs);
#ifdef _WIN32
        logdata.Replace(text("\n"),text("\r\n"));
#endif
        UInt32 tw=logdata.Length()*sizeof(TCHAR);
        fwrite.Write(_zt(logdata),tw,ar);
        if(ar!=tw)
        {
            ew=1;
        }
        logdata=CSysString(&_zt(logdata)[ar/sizeof(TCHAR)]);
        fwrite.Close();
    }
    else
    {
        ew=1;
    }
    if(ew)
    {
        _tprintf(_zt(text("warning: cannot write to log file (")+logFileName+text(")\n")));
    }
    if(force!=0)
    {
        delete plogdata;
        plogdata=0;
    }
}

/*  #########################################################################  */

void logprint(const CSysString&str,UInt32 p=3)
{
    if(p&1)
    {
        _tprintf(str);
    }
    if(p&2)
    {
        log(str);
    }
}

/*  #########################################################################  */

CSysString GetPathFromSwitch(const CSysString&str)
{
    CSysString res=str;
    if(strcmpi_x(a2u(res),L"@")==0)
    {
        res.Delete(0,1);
    }
    if(strcmpi_x(a2u(res),L"--log")==0)
    {
        res.Delete(0,5);
    }
    if(res[0]=='\"'||res[0]=='\'')
    {
        res.Delete(0,1);
    }
    UInt32 l=res.Length();
    if(l)
    {
        if(res[l-1]=='\"'||res[l-1]=='\'')
        {
            res.Delete(l-1,1);
        }
    }
    else
    {
        res=text("");
    }
    return res;
}

/*  #########################################################################  */

int process_mask(const CSysString&base_path,file_proc fp,void*param=0,int split=1,const CSysString&local_path=text(""),const CSysString&mask=text(""))
{
    int EAX=0;
    if(split)
    {
        //this function should work exactly like 7z command-line parser would, hence all the hassle
        CSysString path_t(clean_path(base_path)),mask_t,local_path_t(text("")),last_element;
        if(path_t[0]=='@')
        {
            path_t=GetPathFromSwitch(path_t);
            if(file_exists(path_t)==1)
            {
                UStringVector resultStrings;
                ReadNamesFromListFile(a2u(path_t),resultStrings,codePage);
                for(int i=0;i<resultStrings.Size();i++)
                {
                    EAX+=process_mask(u2a(resultStrings[i]),fp,param);
                }
            }
            return EAX;
        }
        if(DoesNameContainWildCard(a2u(path_t)))
        {
            int i=0,j=path_t.Length();
            for(i=0;i<j;i++)
            {
                if(path_t[i]=='?'||path_t[i]=='*')
                {
                    j=i;
                }
            }
            while(j>0&&path_t[j]!=dirDelimiter0&&path_t[j]!=dirDelimiter1)
            {
                j--;
            }
            if(path_t[j]==dirDelimiter0||path_t[j]==dirDelimiter1)
            {
                mask_t=path_t.Mid(j+1);
                path_t=path_t.Left(j);
            }
            else
            {
                mask_t=path_t;
                path_t=text("");
            }
            if(is_path_abs(path_t)==0)
            {
                local_path_t=path_t;
                path_t=text("");
            }
        }
        else
        {
            mask_t=clean_path(u2a(ExtractFileNameFromPath(a2u(path_t))));
            path_t=clean_path(u2a(ExtractDirPrefixFromPath(a2u(path_t))));
            if(is_path_abs(path_t)==0)
            {
                local_path_t=path_t;
                path_t=text("");
            }
            if(path_t.Compare(text(""))==0)
            {
                if(mask_t.Compare(text("."))==0||mask_t.Compare(text(".."))==0)
                {
                    path_t=mask_t;
                    mask_t=text("*");
                }
            }
        }
//        GetRealPath(local_path_t);
        return process_mask(path_t,fp,param,0,local_path_t,mask_t);
    }
    CSysString path=combine_path(base_path,local_path);
    NWindows::NFile::NFind::CFileInfo fileInfo;
    NWindows::NFile::NFind::CEnumerator match_file(combine_path(path,text("*")));
    while(match_file.Next(fileInfo))
    {
        if(!fileInfo.IsDir())
        {
            if(CompareWildCardWithName(a2u(mask),a2u(combine_path(local_path,fileInfo.Name)))||CompareWildCardWithName(a2u(mask),a2u(fileInfo.Name)))
            {
                EAX+=fp?fp(fileInfo,combine_path(path,fileInfo.Name),combine_path(local_path,fileInfo.Name),param):1;
            }
        }
    }
    NWindows::NFile::NFind::CEnumerator match_dir(combine_path(path,text("*")));
    while(match_dir.Next(fileInfo))
    {
        if(fileInfo.IsDir())
        {
            if(fp)
            {
                if(CompareWildCardWithName(a2u(mask),a2u(combine_path(local_path,fileInfo.Name)))||CompareWildCardWithName(a2u(mask),a2u(fileInfo.Name)))
                {
                    EAX+=fp(fileInfo,combine_path(path,fileInfo.Name),combine_path(local_path,fileInfo.Name),param);
                }
            }
            if(CompareWildCardWithName(a2u(mask),a2u(fileInfo.Name)))
            {
                EAX+=process_mask(base_path,fp,param,split,combine_path(local_path,fileInfo.Name),text("*"));
            }
            else
            {
                EAX+=process_mask(base_path,fp,param,split,combine_path(local_path,fileInfo.Name),mask);
            }
        }
    }
    return EAX;
}

/*  #########################################################################  */

int fenum(const NWindows::NFile::NFind::CFileInfo&fileInfo,const CSysString&fname,const CSysString&local_path,void*x)
{
    finfo*fi=(finfo*)x;
    if(a2u(fileInfo.Name).CompareNoCase(a2u(fi->fileInfo.Name))<0||fi->fileInfo.Name.Compare(text(""))==0)
    {
        fi->fileInfo=fileInfo;
    }
    fi->tcount++;
    if(fileInfo.IsDir())
    {
        if(fi->dirlist)
        {
            fi->dirlist->Add(fname);
        }
        fi->dcount++;
    }
    else
    {
        if(fi->filelist)
        {
            fi->filelist->Add(fname);
        }
        fi->fcount++;
        fi->ttl_fs+=fileInfo.Size;
        if(fileInfo.Size>fi->max_fs)
        {
            fi->max_fs=fileInfo.Size;
        }
        if(fileInfo.Size<fi->min_fs)
        {
            fi->min_fs=fileInfo.Size;
        }
        fi->avg_fs=fi->ttl_fs/fi->fcount;
        if(debug&&fi->debugprint)
        {
            _tprintf(text("processing %i,%i:\t%s\n"),int(fi->fcount),int(fi->ttl_fs),_zt(local_path));
        }
    }
    return 1;
}

/*  #########################################################################  */

int file_exists(const CSysString&fname,CSysString&fdn)
{
    int EAX=0;
    fdn=text("");
    finfo fi;
    memset(((char*)&fi)+sizeof(fi.fileInfo),0,sizeof(finfo)-sizeof(fi.fileInfo));
    if(process_mask(fname,fenum,&fi))
    {
        fdn=fi.fileInfo.Name;
        EAX=fi.fileInfo.IsDir()?2:1;
    }
    if(fname[0]=='@'||DoesNameContainWildCard(a2u(fname)))
    {
        EAX+=4;
    }
    return EAX;
}

/*  #########################################################################  */

int is_t7z(const CSysString&fname)
{
    //0 - not a 7z archive
    //1 - t7z archive
    //2 - 7z archive
    int ist7z=0;
    if(file_exists(fname)==1)
    {
        NWindows::NFile::NIO::CInFile fread;
        if(fread.Open(fname))
        {
            UInt32 ar,offs;
            offs=0;
            fread.SeekToBegin();
            fread.Read(buffer+offs,(crcsz+t7zsig_size+4),ar);
            if(ar<(crcsz+t7zsig_size+4))
            {
                ar-=t7zsig_size+4;
                memset(buffer+offs+ar,0,crcsz-ar);
            }
            offs=crcsz;
            UInt64 foffs;
            fread.GetLength(foffs);
            foffs=foffs<(crcsz+t7zsig_size+4)?0:foffs-(crcsz+t7zsig_size+4);
            fread.Seek(foffs,foffs);
            fread.Read(buffer+offs,(crcsz+t7zsig_size+4),ar);
            if(ar<(crcsz+t7zsig_size+4))
            {
                ar-=t7zsig_size+4;
                memcpy(buffer+crcsz*2+t7zsig_size+4+8,buffer+offs+ar,t7zsig_size+4);
                memset(buffer+offs+ar,0,crcsz-ar);
                memcpy(buffer+crcsz*2+8,buffer+crcsz*2+t7zsig_size+4+8,t7zsig_size+4);
            }
            else
            {
                ar-=t7zsig_size+4;
                memcpy(buffer+crcsz*2+t7zsig_size+4+8,buffer+offs+ar,t7zsig_size+4);
                memcpy(buffer+crcsz*2+8,buffer+crcsz*2+t7zsig_size+4+8,t7zsig_size+4);
            }
            fread.GetLength(foffs);
            foffs-=t7zsig_size+4;
            memcpy(buffer+crcsz*2,&foffs,8);
            if(memcmp(buffer,NArchive::N7z::kSignature,NArchive::N7z::kSignatureSize)==0)
            {
                ist7z=2;
                if(compare_t7zsig(buffer+crcsz*2+4+8))
                {
                    UInt32 _crc32=*((UInt32*)(buffer+crcsz*2+8));
                    *((UInt32*)(buffer+crcsz*2+8))=-1;
                    UInt32 crc32=CrcCalc(buffer,crcsz*2+8+t7zsig_size+4);
                    if(crc32==_crc32)
                    {
                        ist7z=1;
                    }
                }
            }
            fread.Close();
        }
        else
        {
            logprint(text("error: cannot read file: ")+fname+text("\n"));
        }
    }
    return ist7z;
}

/*  #########################################################################  */

bool addt7zsig(const CSysString&fname)
{
    bool EAX=0;
    if(file_exists(fname)==1)
    {
        NWindows::NFile::NIO::CInFile fread;
        if(fread.OpenShared(fname,true))
        {
            UInt32 ar,offs;
            offs=0;
            fread.SeekToBegin();
            fread.Read(buffer+offs,crcsz,ar);
            if(ar<crcsz)
            {
                memset(buffer+offs+ar,0,crcsz-ar);
            }
            offs=crcsz;
            UInt64 foffs;
            fread.GetLength(foffs);
            foffs=foffs<crcsz?0:foffs-crcsz;
            fread.Seek(foffs,foffs);
            fread.Read(buffer+offs,crcsz,ar);
            if(ar<crcsz)
            {
                memset(buffer+offs+ar,0,crcsz-ar);
            }
            fread.GetLength(foffs);
            memcpy(buffer+crcsz*2,&foffs,8);
            *((UInt32*)(buffer+crcsz*2+8))=-1;
            memcpy(buffer+crcsz*2+8+4,get_t7zsig(),t7zsig_size);
            UInt32 crc32=CrcCalc(buffer,crcsz*2+8+t7zsig_size+4);
            NWindows::NFile::NIO::COutFile fwrite;
            if(fwrite.Open(fname,OPEN_EXISTING))
            {
                fwrite.SeekToEnd(foffs);
                fwrite.Write(&crc32,4,ar);
                if(ar!=4)
                {
                    logprint(text("error: cannot write file: ")+fname+text("\n"));
                }
                else
                {
                    fwrite.Write(get_t7zsig(),t7zsig_size,ar);
                    if(ar!=t7zsig_size)
                    {
                        logprint(text("error: cannot write file: ")+fname+text("\n"));
                    }
                    else
                    {
                        EAX=1;
                    }
                }
                fwrite.Close();
            }
            else
            {
                logprint(text("error: cannot write file: ")+fname+text("\n"));
            }
            fread.Close();
        }
        else
        {
            logprint(text("error: cannot read file: ")+fname+text("\n"));
        }
    }
    return EAX;
}

/*  #########################################################################  */

#ifdef _WIN32
STARTUPINFO snfo;
PROCESS_INFORMATION pi;
int cp(const CSysString&app,const CSysString&cmd)
{
    DWORD eax=-1;
    snfo.cb=sizeof(STARTUPINFO);
    memset(((char*)&snfo)+4,0,snfo.cb-4);
    BOOL PS=CreateProcess(app,_zt(cmd),0,0,FALSE,0,0,0,&snfo,&pi);
    if(PS)
    {
        WaitForSingleObject(pi.hProcess,INFINITE);
        GetExitCodeProcess(pi.hProcess,&eax);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return *((int*)&eax);
}
#endif

/*  #########################################################################  */

bool execute(const CSysString&app,const CSysString&cmd)
{
#ifdef _WIN32
    int EAX=cp(app,cmd);
#else
    int EAX=_tsystem(text("\"")+cmd+text("\""));
#endif
    if(debug)
    {
        if(EAX!=0)
        {
            logprint(text("command ")+cmd+text(" returned with error code ")+Int64ToString(EAX));
            switch(EAX)
            {
            case 1:
                logprint(text(" : "));
                logprint(text("Warning (Non fatal error(s)). For example, one or more files were locked by some other application, so they were not compressed."));
                break;
            case 2:
                logprint(text(" : "));
                logprint(text("Fatal error"));
                break;
            case 7:
                logprint(text(" : "));
                logprint(text("Command line error"));
                break;
            case 8:
                logprint(text(" : "));
                logprint(text("Not enough memory for operation"));
                break;
            case 255:
                logprint(text(" : "));
                logprint(text("User stopped the process"));
                break;
            case -1:
                {
                    logprint(text(", system ")+Int64ToString(errno));
                    switch(errno)
                    {
                    case E2BIG:
                        logprint(text(" : "));
                        logprint(text("Argument list (which is system dependent) is too big."));
                        break;
                    case ENOENT:
                        logprint(text(" : "));
                        logprint(text("Command interpreter cannot be found."));
                    case ENOEXEC:
                        logprint(text(" : "));
                        logprint(text("Command-interpreter file has invalid format and is not executable."));
                    case ENOMEM:
                        logprint(text(" : "));
                        logprint(text("Not enough memory is available to execute command; or available memory has been corrupted; or invalid block exists, indicating that process making call was not allocated properly."));
                    }
                }
                break;
            }
            logprint(text("\n"));
        }
    }
    return EAX==0;
}

/*  #########################################################################  */

bool DeleteFileAlways(const CSysString&src)
{
    bool EAX=NWindows::NFile::NDirectory::DeleteFileAlways(src);
    if(!EAX)
    {
        logprint(text("error: cannot delete file: ")+src+text("\n"));
    }
    return EAX;
}

/*  #########################################################################  */

bool MyMoveFile(const CSysString&src,const CSysString&dst)
{
    bool EAX=NWindows::NFile::NDirectory::MyMoveFile(src,dst);
    if(!EAX)
    {
        logprint(text("error: cannot move file: ")+src+text(" -> ")+dst+text("\n"));
    }
    return EAX;
}

/*  #########################################################################  */

bool recompress(const CSysString&fname,bool is7z)
{
    bool eax=0;
    NWindows::NFile::NDirectory::CTempDirectory tmpdir;
    if(tmpdir.Create(text("t7z_")))
    {
        TCHAR*buffer=(TCHAR*)torrent7z::buffer;
        log(text(""),1);
        bool ext=is7z?0:(e7z_exe.Compare(text(""))==0?0:1);
        CSysString*app;
        if(ext)
        {
            app=&e7z_exe;
            _stprintf(buffer,text("\"%s\" x -o\"%s\" -ba -y -- \"%s\""),_zt(e7z_exe),_zt(tmpdir.GetPath()),_zt(fname));
        }
        else
        {
            app=&t7z_exe;
            _stprintf(buffer,text("\"%s\" x --default-priority --log\"%s\" -o\"%s\" -y -- \"%s\""),_zt(t7z_exe),_zt(logFileName),_zt(tmpdir.GetPath()),_zt(fname));
        }
        if(execute(*app,buffer))
        {
            log(text(""),1);
            bool clean=0;
            bool proceed=1;
            CSysString newfn;
            UInt32 i=fname.Length();
            while(i && fname[i]!='.' && fname[i]!=dirDelimiter0 && fname[i]!=dirDelimiter1)
            {
                i--;
            }
            if(fname[i]=='.'&&CompareFileNames(a2u(fname.Mid(i,fname.Length())),L".7z")!=0)
            {
                newfn=fname.Left(i)+text(".7z");
                if(file_exists(newfn))
                {
                    logprint(text("error: file \"")+newfn+text("\" already exists, convert operation will be skipped on file \"")+fname+text("\" to prevent data loss\n"));
                    proceed=0;
                }
                clean=1;
            }
            else
            {
                newfn=fname;
            }
            if(proceed)
            {
                _stprintf(buffer,text("\"%s\" a --default-priority --log\"%s\" --replace-archive -y %s-- \"%s\" \"%s%c*\""),_zt(t7z_exe),_zt(logFileName),g_stripFileNames?text("--strip-filenames "):text(""),_zt(newfn),_zt(tmpdir.GetPath()),dirDelimiter0);
                if(execute(t7z_exe,buffer))
                {
                    eax=1;
                    if(clean)
                    {
                        eax=DeleteFileAlways(fname);
                    }
                }
                else
                {
                    logprint(text("error: unable to recompress file: ")+fname+text("\n"));
                }
            }
        }
        else
        {
            logprint(text("error: unable to uncompress file: ")+fname+text("\n"));
        }
        tmpdir.Remove();
    }
    else
    {
        logprint(text("error: unable to create temporary directory\n"));
    }
    return eax;
}

/*  #########################################################################  */

int nodots(const UString&str)
{
    for(int i=/*0*/1;i<str.Length();i++)
    {
        if(str[i]=='.')
        {
            return 0;
        }
    }
    return 1;
}

/*  #########################################################################  */

#ifndef _WIN32
static void GetArguments(int numArguments,const char*arguments[],UStringVector&parts)
{
    parts.Clear();
    for(int i=0;i<numArguments;i++)
    {
        UString s=MultiByteToUnicodeString(arguments[i]);
        parts.Add(s);
    }
}
#endif

/*  #########################################################################  */

void SetCommandStrings(UStringVector&cs)
{
    static UStringVector*pcs=0;
    if(pcs==0)
    {
        pcs=&cs;
    }
    else
    {
        cs.Clear();
        for(int i=0;i<pcs[0].Size();i++)
        {
            cs.Add(pcs[0][i]);
            if(debug)
            {
                _tprintf(text("\t%s\n"),_zt(u2a(cs[i])));
            }
        }
        if(debug)
        {
            _tprintf(text("\n"));
        }
    }
}

/*  #########################################################################  */

int GetDictionarySize(finfo&fi,bool&solid)
{
    int d=16;
    UInt64 ttl_fs64=fi.ttl_fs;
    ttl_fs64+=1024*1024-1;
    ttl_fs64/=1024*1024;
    UInt64 max_fs64=fi.max_fs;
    max_fs64+=1024*1024-1;
    max_fs64/=1024*1024;
    UInt64 min_fs64=fi.min_fs;
    min_fs64+=1024*1024-1;
    min_fs64/=1024*1024;
    UInt64 avg_fs64=fi.avg_fs;
    avg_fs64+=1024*1024-1;
    avg_fs64/=1024*1024;
    int ttl_fs=(int)fix(ttl_fs64,4);
    UInt64 dmaxfs=max_fs64-avg_fs64;
    UInt64 dminfs=avg_fs64-min_fs64;
    if((fi.fcount<2)||(dmaxfs>dminfs)/*||(avg_fs64>128)*/)
    {
        d=0;
        solid=0;
    }
    else
    {
        d=ttl_fs/2;
        if(max_fs64<60)
        {
            d=min(d,64);
        }
        solid=1;
    }
    //80*11.5+4==924
    d=min(max(16,d),80);
    return d;
    //note that d cannot be greater than ttl_fs
    //7z will reduce d automatically
}

/*  #########################################################################  */

int convert27z(const NWindows::NFile::NFind::CFileInfo&fileInfo,const CSysString&fname,const CSysString&local_path,void*x)
{
    int eax=0;
    cfinfo*fi=(cfinfo*)x;
    if(!fileInfo.IsDir())
    {
        fi->fcount++;
        int ist7z=is_t7z(fname);
        bool pr=g_forceRecompress||ist7z!=1;
        if(1)
        {
            const UInt32 tl=60*1000;
            UInt32 t=GetTickCount();
            static UInt32 ot=t+tl;
            static bool prp=0;
            if(pr&&prp)
            {
                logprint(text("\n"),~2);
            }
            if((pr&&prp)||((t>ot)&&((fi->fcount+128)<fi->fcountt)))
            {
                ot=t+tl;
                logprint(Int64ToString(fi->fcount,7)+text(" out of ")+Int64ToString(fi->fcountt,7)+text(" files(s) processed\n"),~2);
            }
            prp=pr;
        }
        if(pr)
        {
            if(recompress(fname,ist7z!=0))
            {
                fi->fcountp++;
            }
            else
            {
                fi->fcounte++;
            }
            eax=1;
        }
        else
        {
            fi->fcountr++;
        }
    }
    return eax;
}

/*  #########################################################################  */

void print_usage()
{
    logprint(text("error: no parameters, or invalid parameters specified\n"),~2);
    logprint(text("       this is a command line application,\n"),~2);
    logprint(text("       see readme.txt to learn how to use it\n"),~2);
}

/*  #########################################################################  */

int t7z_main
(
#ifndef _WIN32
    int numArguments,const char*arguments[]
#endif
)
{
    UStringVector commandStrings;
#ifdef _WIN32
    NCommandLineParser::SplitCommandLine(GetCommandLineW(),commandStrings);
#else
//    GetArguments(numArguments,arguments,commandStrings);
    extern void mySplitCommandLine(int numArguments,const char *arguments[],UStringVector &parts);
    mySplitCommandLine(numArguments,arguments,commandStrings);
#endif
    int operation_mode=1;
    int no_more_switches=0;
    int replace_archive=0;
    for(int i=1;i<commandStrings.Size();i++)
    {
        if(is_command(commandStrings[i]))
        {
            if(is_allowed(commandStrings[i]))
            {
                operation_mode=3;
            }
            else
            {
                if(commandStrings[i].CompareNoCase(L"a")==0)
                {
                    operation_mode=2;
                }
                else
                {
                    operation_mode=0;
                    logprint(text("error: invalid argument: ")+u2a(commandStrings[i])+text("\n"));
                }
                commandStrings.Delete(i);
                i--;
            }
        }
        else
        {
            if(no_more_switches==0&&is_switch(commandStrings[i]))
            {
                if(commandStrings[i].CompareNoCase(L"--")==0)
                {
                    no_more_switches=1;
                }
                int noprint=0;
                if(commandStrings[i].CompareNoCase(L"--replace-archive")==0)
                {
                    replace_archive=1;
                    noprint=1;
                }
                if(strcmpi_x(commandStrings[i],L"--log")==0)
                {
                    logFileName=GetPathFromSwitch(u2a(commandStrings[i]));
                    noprint=1;
                }
                if(commandStrings[i].CompareNoCase(L"--force-recompress")==0)
                {
                    g_forceRecompress=1;
                    noprint=1;
                }
                if(commandStrings[i].CompareNoCase(L"--batch")==0)
                {
                    g_batch=1;
                    noprint=1;
                }
                if(commandStrings[i].CompareNoCase(L"--default-priority")==0)
                {
                    g_defaultPriority=1;
                    noprint=1;
                }
                if(commandStrings[i].CompareNoCase(L"--strip-filenames")==0)
                {
                    g_stripFileNames=1;
                    noprint=1;
                }
                if(!is_allowed(commandStrings[i]))
                {
                    if(noprint==0)
                    {
                        logprint(text("warning: unsupported switch ignored: ")+u2a(commandStrings[i])+text("\n"),~2);
                    }
                    commandStrings.Delete(i);
                    i--;
                }
            }
        }
    }
#ifdef _WIN32
    if(!g_defaultPriority)
    {
        SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
    }
#endif
    NWindows::NFile::NDirectory::CTempDirectory tmpdir;
    UStringVector t7z_commandStrings;
    SetCommandStrings(t7z_commandStrings);
    t7z_commandStrings.Add(commandStrings[0]);
    {
        CSysString cpath;
        NWindows::NFile::NDirectory::MyGetCurrentDirectory(cpath);
        if(g_firstInstance)
        {
            setenv(text("tmp"),cpath,1);
            if(tmpdir.Create(text("t7z_")))
            {
                setenv(text("tmp"),_zt(tmpdir.GetPath()),1);
            }
            else
            {
                logprint(text("error: unable to create temporary directory\n"));
            }
        }
        TCHAR buffer[64];
        bool nolog=(logFileName.Compare(text(""))==0);
        if(nolog)
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            _stprintf(buffer,text("torrent7z_%d%02d%02d%02d%02d%02d.log"),st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        }
        t7z_exe=text("t7z");
#ifndef _WIN32
        e7z_exe=text("7z");
        if(nolog)
        {
            logFileName=combine_path(cpath,CSysString(buffer));
        }
#else
        if(nolog)
        {
            if(!((!g_batch)&&g_IsParentGui))
            {
                logFileName=combine_path(cpath,CSysString(buffer));
            }
        }
        cpath=text("");
        NWindows::NDLL::MyGetModuleFileName(0,cpath);
        if(cpath.Length())
        {
            t7z_exe=cpath;
            HKEY hKey;
            DWORD tb;
            if(RegCreateKeyEx(HKEY_LOCAL_MACHINE,text("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\")+u2a(ExtractFileNameFromPath(a2u(cpath))),0,0,REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,0,&hKey,&tb)==0)
            {
                RegSetValueEx(hKey,text(""),0,REG_SZ,(LPBYTE)_zt(cpath),(cpath.Length()+1)*sizeof(TCHAR));
                RegCloseKey(hKey);
            }
        }
        if(nolog)
        {
            if((!g_batch)&&g_IsParentGui)
            {
                cpath=clean_path(u2a(ExtractDirPrefixFromPath(a2u(cpath))));
                logFileName=combine_path(cpath,CSysString(buffer));
            }
        }
        e7z_exe=text("7z.exe");
        if(file_exists(e7z_exe)!=1)
        {
            e7z_exe=text("");
            DWORD tb,tp;
            HKEY hKey;
            if(RegOpenKeyEx(HKEY_LOCAL_MACHINE,text("SOFTWARE\\7-Zip"),0,KEY_READ,&hKey)==0)
            {
                tb=tmpbufsize;
                if(RegQueryValueEx(hKey,text("Path"),0,&tp,(LPBYTE)&buffer,&tb)==0)
                {
                    e7z_exe=combine_path(buffer,text("7z.exe"));
                }
            }
        }
        if(file_exists(e7z_exe)!=1)
        {
            e7z_exe=text("");
            DWORD tb,tp;
            HKEY hKey;
            if(RegOpenKeyEx(HKEY_CURRENT_USER,text("SOFTWARE\\7-Zip"),0,KEY_READ,&hKey)==0)
            {
                tb=tmpbufsize;
                if(RegQueryValueEx(hKey,text("Path"),0,&tp,(LPBYTE)&buffer,&tb)==0)
                {
                    e7z_exe=combine_path(buffer,text("7z.exe"));
                }
            }
        }
        if(file_exists(e7z_exe)!=1)
        {
            e7z_exe=text("");
        }
#endif
    }
    if(commandStrings.Size()<2)
    {
        print_usage();
        return NExitCode::kUserError;
    }
    if(operation_mode==0)
    {
        print_usage();
        if(g_firstInstance)
        {
            tmpdir.Remove();
        }
        return NExitCode::kUserError;
    }
    if(operation_mode==1)
    {
        for(int i=1;i<commandStrings.Size();i++)
        {
            if(is_command(commandStrings[i]))
            {
                commandStrings.Delete(i);
                i--;
            }
            else
            {
                if(no_more_switches==0&&is_switch(commandStrings[i]))
                {
                    if(commandStrings[i].CompareNoCase(L"--")==0)
                    {
                        no_more_switches=1;
                    }
                    commandStrings.Delete(i);
                    i--;
                }
            }
        }
        finfo pi;
        memset(((char*)&pi)+sizeof(pi.fileInfo),0,sizeof(finfo)-sizeof(pi.fileInfo));
        CSysStringVector filelist;
        pi.filelist=&filelist;
        for(int i=1;i<commandStrings.Size();i++)
        {
            CSysString fn;
            if(file_exists(u2a(commandStrings[i]))==2)
            {
                fn=combine_path(u2a(commandStrings[i]),text("*.7z"));
            }
            else
            {
                fn=u2a(commandStrings[i]);
            }
            process_mask(fn,fenum,&pi);
        }
        cfinfo fi;
        memset(&fi,0,sizeof(fi));
        fi.fcountt=pi.fcount;
        for(int i=0;i<filelist.Size();i++)
        {
            process_mask(filelist[i],convert27z,&fi);
        }
        if(fi.fcount==0)
        {
            logprint(text("\nerror: no file(s) to process\n"));
        }
        else
        {
            int print=fi.fcounte?~0:~2;
            logprint(text("\n")+Int64ToString(fi.fcount)+text(" file(s) processed, ")+Int64ToString(fi.fcountp)+text(" files(s) converted\n"),print);
            if(fi.fcountr)
            {
                logprint(Int64ToString(fi.fcountr)+text(" file(s) were already in t7z format\n"),print);
            }
            logprint(text("\n"),print);
            if(fi.fcounte)
            {
                logprint(text("there were ")+Int64ToString(fi.fcounte)+text(" error(s)\n"),print);
                logprint(text("check error log for details\n\n"),~2);
            }
        }
    }
    if(operation_mode==2)
    {
        UString*archive,*pd,*sd;
        t7z_commandStrings.Add(L"a");
        t7z_commandStrings.Add(L"-r");
        t7z_commandStrings.Add(L"-t7z");
        t7z_commandStrings.Add(L"-mx=9");
        t7z_commandStrings.Add(L"");
        sd=&t7z_commandStrings[t7z_commandStrings.Size()-1];
        t7z_commandStrings.Add(L"-mf=on");
        t7z_commandStrings.Add(L"-mhc=on");
        t7z_commandStrings.Add(L"-mhe=off");
        t7z_commandStrings.Add(L"-mmt=2");
        t7z_commandStrings.Add(L"-mtc=off");
        t7z_commandStrings.Add(L"-mta=off");
        t7z_commandStrings.Add(L"-mtm=off");
        t7z_commandStrings.Add(L"");
        pd=&t7z_commandStrings[t7z_commandStrings.Size()-1];
        t7z_commandStrings.Add(L"-ba");
        no_more_switches=0;
        for(int i=1;i<commandStrings.Size();i++)
        {
            if(is_command(commandStrings[i]))
            {
                commandStrings.Delete(i);
                i--;
            }
            else
            {
                if(no_more_switches==0&&is_switch(commandStrings[i]))
                {
                    if(commandStrings[i].CompareNoCase(L"--")==0)
                    {
                        no_more_switches=1;
                    }
                    else
                    {
                        t7z_commandStrings.Add(commandStrings[i]);
                    }
                    commandStrings.Delete(i);
                    i--;
                }
            }
        }
        t7z_commandStrings.Add(L"--");
        t7z_commandStrings.Add(L"");
        archive=&t7z_commandStrings[t7z_commandStrings.Size()-1];
        finfo fi;
        memset(((char*)&fi)+sizeof(fi.fileInfo),0,sizeof(finfo)-sizeof(fi.fileInfo));
        fi.debugprint=1;
        bool fnotfound=0;
        int fe=0;
        for(int i=1;i<commandStrings.Size();i++)
        {
            if(fe==0)
            {
                if(replace_archive)
                {
                    archive[0]=commandStrings[i];
                    fe=1;
                    continue;
                }
                else
                {
                    CSysString fn;
                    fe=file_exists(u2a(commandStrings[i]),fn);
                    if(fe==0)
                    {
                        archive[0]=commandStrings[i];
                    }
                    else
                    {
                        if(fe!=4)
                        {
                            if(fe&6)
                            {
                                archive[0]=a2u(fn);
                            }
                            else
                            {
                                archive[0]=a2u(clean_path(combine_path(u2a(ExtractDirPrefixFromPath(commandStrings[i])),fn)));
                            }
                            if(((fe&2)!=0)||(is_t7z(u2a(archive[0]))==0))
                            {
                                archive[0]+=L".7z";
                                t7z_commandStrings.Add(commandStrings[i]);
                                if(process_mask(u2a(commandStrings[i]),fenum,&fi)==0)
                                {
                                    fnotfound=1;
                                    logprint(text("error: no files match: ")+u2a(commandStrings[i])+text("\n"));
                                }
                            }
                        }
                    }
                    if(fe==4)
                    {
                        fe=0;
                    }
                    else
                    {
                        fe=1;
                        continue;
                    }
                }
            }
            t7z_commandStrings.Add(commandStrings[i]);
            if(process_mask(u2a(commandStrings[i]),fenum,&fi)==0)
            {
                fnotfound=1;
                logprint(text("error: no files match: ")+u2a(commandStrings[i])+text("\n"));
            }
        }
        if(fi.fcount==0||fnotfound)
        {
            if(fi.fcount==0)
            {
                logprint(text("error: no files to process\n"));
            }
            if(g_firstInstance)
            {
                tmpdir.Remove();
            }
            return NExitCode::kUserError;
        }
        bool solid;
        int ds=GetDictionarySize(fi,solid);
        if(solid)
        {
            sd[0]=UString(L"-ms=")+a2u(Int64ToString(fix(fi.fcount+128,1024)))+UString(L"f4g");
            if(debug)
            {
                _tprintf(text("\t***\tsolid mode activated\n"));
            }
        }
        else
        {
            //sd[0]=UString(L"-ms=off");//disabled for now
            sd[0]=UString(L"-ms=")+a2u(Int64ToString(fix(fi.fcount+128,1024)))+UString(L"f4g");
            if(debug)
            {
                _tprintf(text("\t***\tno use for solid mode\n"));
            }
        }
        pd[0]=UString(L"-m0=LZMA:a1:d")+a2u(Int64ToString(ds))+UString(L"m:mf=BT4:fb128:mc80:lc4:lp0:pb2");
        if(archive[0][0]==0)
        {
            archive[0]=L"default";
        }
        if(nodots(archive[0]))
        {
            archive[0]+=L".7z";
        }
        if(!replace_archive)
        {
            fe=file_exists(u2a(archive[0]));
            if(fe!=0)
            {
                logprint(text("error: file \"")+u2a(archive[0])+text("\" already exists, update operation is not supported, aborting\n"));
                if(g_firstInstance)
                {
                    tmpdir.Remove();
                }
                return NExitCode::kUserError;
            }
        }
        UString rarchive=archive[0];
        archive[0]+=UString(L".tmp");
        fe=file_exists(u2a(archive[0]));
        if(fe!=0)
        {
            logprint(text("error: file \"")+u2a(archive[0])+text("\" already exists(manually delete .tmp file, probably a leftover)\n"));
            if(g_firstInstance)
            {
                tmpdir.Remove();
            }
            return NExitCode::kUserError;
        }
        if(fi.tcount==1)
        {
            g_singleFile=1;
        }
        if(!g_singleFile)
        {
            g_stripFileNames=0;
        }
        int eax=main3(
#ifndef _WIN32
        argc,newargs
#endif
        );
        if(eax==0)
        {
            eax=NExitCode::kFatalError;
            if(addt7zsig(u2a(archive[0])))
            {
                bool fne=1;
                if(replace_archive&&file_exists(u2a(rarchive)))
                {
                    fne=DeleteFileAlways(u2a(rarchive));
                }
                if(fne)
                {
                    if(g_stripFileNames&&g_singleFile)
                    {
                        //or else file extension most likely will be lost
                        rarchive=a2u(clean_path(combine_path(u2a(ExtractDirPrefixFromPath(rarchive)),fi.fileInfo.Name)+CSysString(text(".7z"))));
                    }
                    if(MyMoveFile(u2a(archive[0]),u2a(rarchive)))
                    {
                        eax=0;
                    }
                }
            }
        }
        else
        {
            if(file_exists(u2a(archive[0])))
            {
                DeleteFileAlways(u2a(archive[0]));
            }
        }
        if(g_firstInstance)
        {
            tmpdir.Remove();
        }
        return eax;
    }
    if(operation_mode==3)
    {
        t7z_commandStrings.Insert(1,L"-ba");
        t7z_commandStrings.Insert(1,L"-r");
        for(int i=1;i<commandStrings.Size();i++)
        {
            t7z_commandStrings.Add(commandStrings[i]);
        }
        int eax=main3(
#ifndef _WIN32
        argc,newargs
#endif
        );
        if(g_firstInstance)
        {
            tmpdir.Remove();
        }
        return eax;
    }
    if(g_firstInstance)
    {
        tmpdir.Remove();
    }
    return NExitCode::kUserError;
}

/*  #########################################################################  */

#ifdef _WIN32
#if !defined(_UNICODE) || !defined(_WIN64)
static inline bool IsItWindowsNT()
{
    OSVERSIONINFO versionInfo;
    versionInfo.dwOSVersionInfoSize=sizeof(versionInfo);
    if(!GetVersionEx(&versionInfo))
    {
        return false;
    }
    return (versionInfo.dwPlatformId==VER_PLATFORM_WIN32_NT);
}
#endif
#endif

/*  #########################################################################  */

}
using namespace torrent7z;

/*  #########################################################################  */

int MY_CDECL main
(
#ifndef _WIN32
    int numArguments,const char*arguments[]
#endif
)
{
#ifdef _UNICODE
#ifndef _WIN64
    if(!IsItWindowsNT())
    {
        _tprintf(text("This program requires Windows NT/2000/2003/2008/XP/Vista\n"));
        return NExitCode::kFatalError;
    }
#endif
#endif
#ifdef _WIN32
    if(!debug)
    {
        SetErrorMode(SEM_FAILCRITICALERRORS);
    }
    SetLastError(0);
#endif
    g_stripFileNames=0;
    g_singleFile=0;
    g_forceRecompress=0;
    g_batch=0;
    codePage=CP_UTF8;
    logFileName=text("");
    buffer=new char[tmpbufsize];
    if(buffer==0)
    {
        logprint(text("error: Can't allocate required memory!\n"));
        return NExitCode::kFatalError;
    }
    g_IsParentGui=IsParentGui()!=0;
    g_firstInstance=0;
    g_defaultPriority=0;
#ifdef _UNICODE
    CSysString blk(text(""));
    if(blk.Compare(CSysString(_tgetenv(_zt(MultiByteToUnicodeString(t7zsig_str,CP_ACP)))))==0)
    {
        setenv(_zt(MultiByteToUnicodeString(t7zsig_str,CP_ACP)),text("1"),1);
        logprint(text("\n")+MultiByteToUnicodeString(t7zsig_str,CP_ACP)+text("/")+text(__TIMESTAMP__)+text("\n"),~2);
        logprint(text("using ")+MultiByteToUnicodeString(k7zCopyrightString,CP_ACP)+text("\n\n"),~2);
        g_firstInstance=1;
    }
#else
    CSysString blk(text(""));
    if(blk.Compare(CSysString(_tgetenv(t7zsig_str)))==0)
    {
        setenv(t7zsig_str,text("1"),1);
        logprint(text("\n")+CSysString(t7zsig_str)+text("/")+text(__TIMESTAMP__)+text("\n"),~2);
        logprint(text("using ")+CSysString(k7zCopyrightString)+text("\n\n"),~2);
        g_firstInstance=1;
    }
#endif
    //logprint(text("executing command line: ")+CSysString(GetCommandLine())+text("\n"),~1);
    //log execution started:
    int EAX=t7z_main(
#ifndef _WIN32
    numArguments,arguments
#endif
    );
    //log time taken
    log(text(""),1);
#ifdef _WIN32
    if((!g_batch)&&g_IsParentGui)
    {
        _tprintf(text("\nPress any key to continue . . . "));
        _gettch();
        _tprintf(text("\n"));
    }
#endif
    if(buffer)
    {
        delete[] buffer;
    }
    return EAX;
}

/*  #########################################################################  */
