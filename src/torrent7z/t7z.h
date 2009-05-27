namespace torrent7z
{

extern const char*t7zsig;
extern const char*t7zsig_str;
const int t7zsig_size=16+1+9+4+4;

extern char*buffer;
const int tmpbufsize=(32+1)*1024*sizeof(TCHAR);

#ifdef _WIN32
int IsParentGui();
#endif

}