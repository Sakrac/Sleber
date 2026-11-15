#ifndef _SBX_FILE_H_
#define _SBX_FILE_H_

struct Sleber;
typedef struct Sleber Sleber;

bool LoadSbxFile(Sleber* app, const char* path_utf8);
bool SaveSbxFile(Sleber* app, const char* path_utf8);

#endif //_SBX_FILE_H_
