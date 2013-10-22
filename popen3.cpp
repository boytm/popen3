
#if defined(WIN32)

#if defined(__MINGW32__)
#include <sys/fcntl.h>
#endif
#include <io.h>
#include <tchar.h>
#include <windows.h>

#include <stdio.h>

#define POPEN_PIPE_BUFFER_SIZE 4096
#define popen3_fatal(x)  _ftprintf(stderr, _T(x)); return -1;

/*
 * Display the error message of Windows system call 
 */
void ExitOnSystemCallError(LPCTSTR lpszFunction) 
{ 
    /* Retrieve the system error message for the last-error code */
    TCHAR buff[1024] = {};
    DWORD dw = GetLastError(); 

    FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buff,
        sizeof(buff)/sizeof(buff[0]), 
        NULL );

    /* Display the error message and exit the process */
    _ftprintf(stderr, _T("%s\n%s\n"), buff, lpszFunction);

    ExitProcess(dw); 
}

/*
 * Convert a Windows file handle to crt type FILE*
 */
FILE* fhopen(HANDLE hFile, const char *zMode)
{
    int fd = _open_osfhandle((intptr_t)hFile, _O_BINARY);

    if( fd != -1)
        return _fdopen(fd, zMode);
    else
        return NULL;
}


/*
 * Spawn child process and redirect its io to our handles
 */
static DWORD SpawnChildWithHandle(LPCTSTR zCmd, HANDLE hIn, HANDLE hOut, HANDLE hErr)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    BOOL success;
	TCHAR lpCmdLine[32 * 1024] = {'0'};

	_tcscpy(lpCmdLine, zCmd);

    memset((void *)&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof (STARTUPINFO);

    /*
     * Indicate the hStdInput, hStdOutput, and hStdError members are valid. 
     */
    si.dwFlags = STARTF_USESTDHANDLES;

    si.hStdInput  = hIn;
    si.hStdOutput = hOut;
    si.hStdError  = hErr;

    /*
     * Ensure stdio inheritable.
     */
    success = SetHandleInformation(si.hStdInput, HANDLE_FLAG_INHERIT, TRUE);
    if(!success) {
        ExitOnSystemCallError("SetHandleInformation failed");
    }

    success = SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, TRUE);
    if(!success) {
        ExitOnSystemCallError("SetHandleInformation failed");
    }

    success = SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, TRUE);
    if(!success) {
        ExitOnSystemCallError("SetHandleInformation failed");
    }

    /* 
     * Create child process
     */
    success = CreateProcess(NULL,	/* LPCSTR address of module name */
            lpCmdLine,           /* LPCSTR address of command line. 
								 The Unicode version of this function, 
								 CreateProcessW, can modify the contents 
								 of this string. */
            NULL,		/* Process security attributes */
            NULL,		/* Thread security attributes */
            TRUE,		/* Inheritable Handes inherited. */
            0,		    /* DWORD creation flags  */
            NULL,       /* Use parent environment block */
            NULL,		/* Address of current directory name */
            &si,   /* Address of STARTUPINFO  */
            &pi);	/* Address of PROCESS_INFORMATION   */

    if(!success){
        ExitOnSystemCallError("CreateProcess failed");
    }

    /* Close process and thread handles. */
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );

    return pi.dwProcessId;
}

static DWORD SpawnChild(LPCTSTR zCmd, HANDLE *hIn, HANDLE *hOut, HANDLE *hErr)
{
    HANDLE hChildStdinRd, hChildStdinWr, 
           hChildStdoutRd, hChildStdoutWr,
           hChildStderrRd, hChildStderrWr;

    SECURITY_ATTRIBUTES saAttr;

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
    saAttr.bInheritHandle = TRUE; /* Set the bInheritHandle flag so pipe handles are inherited. */
    saAttr.lpSecurityDescriptor = NULL; 

    /* Create a pipe for the child process's STDIN. */
    if (! CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, POPEN_PIPE_BUFFER_SIZE)) 
        ExitOnSystemCallError("Stdin pipe creation failed\n"); 

    /* Ensure the write handle to the pipe for STDIN is not inherited. */
    SetHandleInformation( hChildStdinWr, HANDLE_FLAG_INHERIT, FALSE);

    /* Create a pipe for the child process's STDOUT. */
    if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, POPEN_PIPE_BUFFER_SIZE)) 
        ExitOnSystemCallError("Stdout pipe creation failed\n"); 

    /* Ensure the read handle to the pipe for STDOUT is not inherited. */
    SetHandleInformation( hChildStdoutRd, HANDLE_FLAG_INHERIT, FALSE);

    /* Create a pipe for the child process's STDERR. */
    if (! CreatePipe(&hChildStderrRd, &hChildStderrWr, &saAttr, POPEN_PIPE_BUFFER_SIZE)) 
        ExitOnSystemCallError("Stderr pipe creation failed\n"); 

    /* Ensure the write handle to the pipe for STDERR is not inherited. */
    SetHandleInformation( hChildStderrRd, HANDLE_FLAG_INHERIT, FALSE);

    /* Spawn child and redirect its io */
    DWORD child_pid = SpawnChildWithHandle(zCmd, hChildStdinRd, hChildStdoutWr, hChildStderrWr);

    /* Close pipe handles that used by child to read and write */
    CloseHandle(hChildStdinRd); 
    CloseHandle(hChildStdoutWr);
    CloseHandle(hChildStderrWr);

    *hIn = hChildStdinWr;
    *hOut = hChildStdoutRd;
    *hErr = hChildStderrRd;

    return child_pid;
}

#ifdef __cplusplus
DWORD popen3(const char *zCmd, int *infd, int *outfd, int *errfd)
{
	HANDLE in, out, err;
	DWORD child_pid = SpawnChild(zCmd, &in, &out, &err);

	if(child_pid == 0){
		popen3_fatal("create child process failed");
	}

	*infd = _open_osfhandle((intptr_t)in, _O_BINARY);
	*outfd = _open_osfhandle((intptr_t)out, _O_BINARY);
	*errfd = _open_osfhandle((intptr_t)err, _O_BINARY);

	if( (*infd) == -1 || (*outfd) == -1 || (*errfd) == -1){
		popen3_fatal("fhopen failed!");
	}

	return child_pid;
}
#endif

// returned FILE* is disabled buffer
DWORD popen3(const char *zCmd, FILE **ppIn, FILE **ppOut, FILE **ppErr)
{
    HANDLE in, out, err;
    DWORD child_pid = SpawnChild(zCmd, &in, &out, &err);

    if(child_pid == 0){
        popen3_fatal("create child process failed");
    }

    *ppIn = fhopen(in, "wb");
    setvbuf(*ppIn, NULL, _IONBF, 0); // No buffer is used
    *ppOut = fhopen(out, "rb");
    setvbuf(*ppOut, NULL, _IONBF, 0); // No buffer is used
	*ppErr = fhopen(err, "rb");
    setvbuf(*ppErr, NULL, _IONBF, 0); // No buffer is used

    if( (*ppIn) == NULL || (*ppOut) == NULL || (*ppErr) == NULL){
        popen3_fatal("fhopen failed!");
    }

    return child_pid;
}

#ifdef TEST_POEPN3
int main(int argc, char **argv)
{
	FILE *in, *out, *err;

	int pid = popen3(argv[1], &in, &out, &err);

	const char *to_write_child = argv[2];

    //for(int idx = 0; idx < 1000; ++idx)
    {
        // write 
        fwrite(to_write_child, strlen(to_write_child), 1, in);
        fwrite("\n\n", 2, 1, in);

        // wait child exit;
        //fflush(in);
    }

    Sleep(100);
    //fclose(in);

	char buffer[4096] = {'\0'};

	fread(buffer, 4096, 1, out);
	fprintf(stdout, "read [%s] from stdout\n", buffer);

    memset(buffer, 0, 4096);
	fread(buffer, 4096, 1, err);
	fprintf(stdout, "read [%s] from stderr\n", buffer);


	return 0;
};
#endif /* TEST_POEPN3 */
#endif /* WIN32 */
