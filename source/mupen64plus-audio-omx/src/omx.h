/**
   Based on code
   Copyright (C) 2007-2009 STMicroelectronics
   Copyright (C) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
   under the LGPL
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#include <OMX_Core.h>
#include <OMX_Component.h>
#include <OMX_Types.h>
#include <OMX_Audio.h>

#ifdef RASPBERRY_PI
#include <bcm_host.h>
#include <IL/OMX_Broadcom.h>
#endif

OMX_ERRORTYPE err;
OMX_HANDLETYPE handle;

OMX_U32 nBufferSize;
int nBuffers;

pthread_mutex_t mutex;
OMX_STATETYPE currentState = OMX_StateLoaded;
pthread_cond_t stateCond;

void waitFor(OMX_STATETYPE state) {
    pthread_mutex_lock(&mutex);
    while (currentState != state)
	pthread_cond_wait(&stateCond, &mutex);
    pthread_mutex_unlock(&mutex);
}

void wakeUp(OMX_STATETYPE newState) {
    pthread_mutex_lock(&mutex);
    currentState = newState;
    pthread_cond_signal(&stateCond);
    pthread_mutex_unlock(&mutex);
}

pthread_mutex_t empty_mutex;
int emptyState = 0;
OMX_BUFFERHEADERTYPE* pEmptyBuffer;
pthread_cond_t emptyStateCond;

void waitForEmpty() {
    pthread_mutex_lock(&empty_mutex);
    while (emptyState == 1)
	pthread_cond_wait(&emptyStateCond, &empty_mutex);
    emptyState = 1;
    pthread_mutex_unlock(&empty_mutex);
}

void wakeUpEmpty(OMX_BUFFERHEADERTYPE* pBuffer) {
    pthread_mutex_lock(&empty_mutex);
    emptyState = 0;
    pEmptyBuffer = pBuffer;
    pthread_cond_signal(&emptyStateCond);
    pthread_mutex_unlock(&empty_mutex);
}

void mutex_init() {
    int n = pthread_mutex_init(&mutex, NULL);
    if ( n != 0) {
	fprintf(stderr, "Can't init state mutex\n");
    }
    n = pthread_mutex_init(&empty_mutex, NULL);
    if ( n != 0) {
	fprintf(stderr, "Can't init empty mutex\n");
    }
}

static void display_help() {
    fprintf(stderr, "Usage: render input_file");
}


/** Gets the file descriptor's size
 * @return the size of the file. If size cannot be computed
 * (i.e. stdin, zero is returned)
 */
static int getFileSize(int fd) {

    struct stat input_file_stat;
    int err;

    /* Obtain input file length */
    err = fstat(fd, &input_file_stat);
    if(err){
	fprintf(stderr, "fstat failed",0);
	exit(-1);
    }
    return input_file_stat.st_size;
}

OMX_ERRORTYPE cEventHandler(
			    OMX_HANDLETYPE hComponent,
			    OMX_PTR pAppData,
			    OMX_EVENTTYPE eEvent,
			    OMX_U32 Data1,
			    OMX_U32 Data2,
			    OMX_PTR pEventData) {

    fprintf(stderr, "Hi there, I am in the %s callback\n", __func__);
    if(eEvent == OMX_EventCmdComplete) {
	if (Data1 == OMX_CommandStateSet) {
	    fprintf(stderr, "Component State changed in ", 0);
	    switch ((int)Data2) {
	    case OMX_StateInvalid:
		fprintf(stderr, "OMX_StateInvalid\n", 0);
		break;
	    case OMX_StateLoaded:
		fprintf(stderr, "OMX_StateLoaded\n", 0);
		break;
	    case OMX_StateIdle:
		fprintf(stderr, "OMX_StateIdle\n",0);
		break;
	    case OMX_StateExecuting:
		fprintf(stderr, "OMX_StateExecuting\n",0);
		break;
	    case OMX_StatePause:
		fprintf(stderr, "OMX_StatePause\n",0);
		break;
	    case OMX_StateWaitForResources:
		fprintf(stderr, "OMX_StateWaitForResources\n",0);
		break;
	    }
	    wakeUp((int) Data2);
	} else  if (Data1 == OMX_CommandPortEnable){
     
	} else if (Data1 == OMX_CommandPortDisable){
     
	}
    } else if(eEvent == OMX_EventBufferFlag) {
	if((int)Data2 == OMX_BUFFERFLAG_EOS) {
     
	}
    } else {
	fprintf(stderr, "Param1 is %i\n", (int)Data1);
	fprintf(stderr, "Param2 is %i\n", (int)Data2);
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE cEmptyBufferDone(
			       OMX_HANDLETYPE hComponent,
			       OMX_PTR pAppData,
			       OMX_BUFFERHEADERTYPE* pBuffer) {

    fprintf(stderr, "Hi there, I am in the %s callback.\n", __func__);
    if (bEOS) {
	fprintf(stderr, "Buffers emptied, exiting\n");
    }
    wakeUpEmpty(pBuffer);
    fprintf(stderr, "Exiting callback\n");

    return OMX_ErrorNone;
}

OMX_CALLBACKTYPE callbacks  = { .EventHandler = cEventHandler,
				.EmptyBufferDone = cEmptyBufferDone,
};

void printState() {
    OMX_STATETYPE state;
    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "Error on getting state\n");
	exit(1);
    }
    switch (state) {
    case OMX_StateLoaded: fprintf(stderr, "StateLoaded\n"); break;
    case OMX_StateIdle: fprintf(stderr, "StateIdle\n"); break;
    case OMX_StateExecuting: fprintf(stderr, "StateExecuting\n"); break;
    case OMX_StatePause: fprintf(stderr, "StatePause\n"); break;
    case OMX_StateWaitForResources: fprintf(stderr, "StateWiat\n"); break;
    default:  fprintf(stderr, "State unknown\n"); break;
    }
}


static void setHeader(OMX_PTR header, OMX_U32 size) {
    /* header->nVersion */
    OMX_VERSIONTYPE* ver = (OMX_VERSIONTYPE*)(header + sizeof(OMX_U32));
    /* header->nSize */
    *((OMX_U32*)header) = size;

    /* for 1.2
       ver->s.nVersionMajor = OMX_VERSION_MAJOR;
       ver->s.nVersionMinor = OMX_VERSION_MINOR;
       ver->s.nRevision = OMX_VERSION_REVISION;
       ver->s.nStep = OMX_VERSION_STEP;
    */
    ver->s.nVersionMajor = specVersion.s.nVersionMajor;
    ver->s.nVersionMinor = specVersion.s.nVersionMinor;
    ver->s.nRevision = specVersion.s.nRevision;
    ver->s.nStep = specVersion.s.nStep;
}

/**
 * Disable unwanted ports, or we can't transition to Idle state
 */
void disablePort(OMX_INDEXTYPE paramType) {
    OMX_PORT_PARAM_TYPE param;
    int nPorts;
    int startPortNumber;
    int n;

    setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));
    err = OMX_GetParameter(handle, paramType, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting OMX_PORT_PARAM_TYPE parameter\n", 0);
	exit(1);
    }
    startPortNumber = ((OMX_PORT_PARAM_TYPE)param).nStartPortNumber;
    nPorts = ((OMX_PORT_PARAM_TYPE)param).nPorts;
    if (nPorts > 0) {
	fprintf(stderr, "Other has %d ports\n", nPorts);
	/* and disable it */
	for (n = 0; n < nPorts; n++) {
	    err = OMX_SendCommand(handle, OMX_CommandPortDisable, n + startPortNumber, NULL);
	    if (err != OMX_ErrorNone) {
		fprintf(stderr, "Error on setting port to disabled\n");
		exit(1);
	    }
	}
    }
}

#ifdef RASPBERRY_PI
/* For the RPi name can be "hdmi" or "local" */
void setOutputDevice(const char *name) {
   int32_t success = -1;
   OMX_CONFIG_BRCMAUDIODESTINATIONTYPE arDest;

   if (name && strlen(name) < sizeof(arDest.sName)) {
       setHeader(&arDest, sizeof(OMX_CONFIG_BRCMAUDIODESTINATIONTYPE));
       strcpy((char *)arDest.sName, name);
       
       err = OMX_SetParameter(handle, OMX_IndexConfigBrcmAudioDestination, &arDest);
       if (err != OMX_ErrorNone) {
	   fprintf(stderr, "Error on setting audio destination\n");
	   exit(1);
       }
   }
}
#endif

void setPCMMode(int startPortNumber) {
    OMX_AUDIO_PARAM_PCMMODETYPE sPCMMode;
 
    setHeader(&sPCMMode, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    sPCMMode.nPortIndex = startPortNumber;
    sPCMMode.nSamplingRate = 48000;
    sPCMMode.nChannels;

    err = OMX_SetParameter(handle, OMX_IndexParamAudioPcm, &sPCMMode);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "PCM mode unsupported\n");
	return;
    } else {
	fprintf(stderr, "PCM mode supported\n");
	fprintf(stderr, "PCM sampling rate %d\n", sPCMMode.nSamplingRate);
	fprintf(stderr, "PCM nChannels %d\n", sPCMMode.nChannels);
    } 
}

int main(int argc, char** argv) {

    OMX_PORT_PARAM_TYPE param;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_AUDIO_PORTDEFINITIONTYPE sAudioPortDef;
    OMX_AUDIO_PARAM_PORTFORMATTYPE sAudioPortFormat;
    OMX_AUDIO_PARAM_PCMMODETYPE sPCMMode;
    OMX_BUFFERHEADERTYPE **inBuffers;

#ifdef RASPBERRY_PI
    char *componentName = "OMX.broadcom.audio_render";
#endif
#ifdef LIM
    char *componentName = "OMX.limoi.alsa_sink";
#endif
    unsigned char name[OMX_MAX_STRINGNAME_SIZE];
    OMX_UUIDTYPE uid;
    int startPortNumber;
    int nPorts;
    int n;

# ifdef RASPBERRY_PI
    bcm_host_init();
# endif

    fprintf(stderr, "Thread id is %p\n", pthread_self());
    if(argc < 2){
	display_help();
	exit(1);
    }

    fd = open(argv[1], O_RDONLY);
    if(fd < 0){
	perror("Error opening input file\n");
	exit(1);
    }
    filesize = getFileSize(fd);


    err = OMX_Init();
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_Init() failed\n", 0);
	exit(1);
    }
    /** Ask the core for a handle to the audio render component
     */
    err = OMX_GetHandle(&handle, componentName, NULL /*app private data */, &callbacks);
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_GetHandle failed\n", 0);
	exit(1);
    }
    err = OMX_GetComponentVersion(handle, name, &compVersion, &specVersion, &uid);
    if(err != OMX_ErrorNone) {
	fprintf(stderr, "OMX_GetComponentVersion failed\n", 0);
	exit(1);
    }

    /** disable other ports */
    disablePort(OMX_IndexParamOtherInit);

    /** Get audio port information */
    setHeader(&param, sizeof(OMX_PORT_PARAM_TYPE));
    err = OMX_GetParameter(handle, OMX_IndexParamAudioInit, &param);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting OMX_PORT_PARAM_TYPE parameter\n", 0);
	exit(1);
    }
    startPortNumber = ((OMX_PORT_PARAM_TYPE)param).nStartPortNumber;
    nPorts = ((OMX_PORT_PARAM_TYPE)param).nPorts;
    if (nPorts > 1) {
	fprintf(stderr, "Render device has more than one port\n");
	exit(1);
    }

    /* Get and check port information */
    setHeader(&sPortDef, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    sPortDef.nPortIndex = startPortNumber;
    err = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in getting OMX_PORT_DEFINITION_TYPE parameter\n", 0);
	exit(1);
    }
    if (sPortDef.eDomain != OMX_PortDomainAudio) {
	fprintf(stderr, "Port %d is not an audio port\n", startPortNumber);
	exit(1);
    } 
      
    if (sPortDef.eDir != OMX_DirInput) {
	fprintf(stderr, "Port is not an input port\n");
	exit(1);
    }
    if (sPortDef.format.audio.eEncoding == OMX_AUDIO_CodingPCM) {
	fprintf(stderr, "Port encoding is PCM\n"); 
    }    else {
	fprintf(stderr, "Port has unknown encoding\n");
    }

    /* Create minimum number of buffers for the port */
    nBuffers = sPortDef.nBufferCountActual = sPortDef.nBufferCountMin;
    fprintf(stderr, "Number of bufers is %d\n", nBuffers);
    err = OMX_SetParameter(handle, OMX_IndexParamPortDefinition, &sPortDef);
    if(err != OMX_ErrorNone){
	fprintf(stderr, "Error in setting OMX_PORT_PARAM_TYPE parameter\n", 0);
	exit(1);
    }
    if (sPortDef.bEnabled) {
	fprintf(stderr, "Port is enabled\n");
    } else {
	fprintf(stderr, "Port is not enabled\n");
    }

    /* call to put state into idle before allocating buffers */
    err = OMX_SendCommand(handle, OMX_CommandStateSet, OMX_StateIdle, NULL);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "Error on setting state to idle\n");
	exit(1);
    }
 
    err = OMX_SendCommand(handle, OMX_CommandPortEnable, startPortNumber, NULL);
    if (err != OMX_ErrorNone) {
	fprintf(stderr, "Error on setting port to enabled\n");
	exit(1);
    }

    /* Configure buffers for the port */
    nBufferSize = sPortDef.nBufferSize;
    fprintf(stderr, "%d buffers of size is %d\n", nBuffers, nBufferSize);

    inBuffers = malloc(nBuffers * sizeof(OMX_BUFFERHEADERTYPE *));
    if (inBuffers == NULL) {
	fprintf(stderr, "Can't allocate buffers\n");
	exit(1);
    }

    for (n = 0; n < nBuffers; n++) {
	err = OMX_AllocateBuffer(handle, inBuffers+n, startPortNumber, NULL,
				 nBufferSize);
	if (err != OMX_ErrorNone) {
	    fprintf(stderr, "Error on AllocateBuffer in 1%i\n", err);
	    exit(1);
	}
    }
    /* Make sure we've reached Idle state */
    waitFor(OMX_StateIdle);
    
    /* Now try to switch to Executing state */
    err = OMX_SendCommand(handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    if(err != OMX_ErrorNone){
	exit(1);
    }

    /* One buffer is the minimum for Broadcom component, so use that */
    pEmptyBuffer = inBuffers[0];
    emptyState = 1;
    /* Fill and empty buffer */
    for (;;) {
	int data_read = read(fd, pEmptyBuffer->pBuffer, nBufferSize);
	pEmptyBuffer->nFilledLen = data_read;
	pEmptyBuffer->nOffset = 0;
	filesize -= data_read;
	if (data_read <= 0) {
	    fprintf(stderr, "In the %s no more input data available\n", __func__);
	    pEmptyBuffer->nFilledLen=0;
	    pEmptyBuffer->nFlags = OMX_BUFFERFLAG_EOS;
	    bEOS=OMX_TRUE;
	}
	fprintf(stderr, "Emptying again buffer %p %d bytes, %d to go\n", pEmptyBuffer, data_read, filesize);
	err = OMX_EmptyThisBuffer(handle, pEmptyBuffer);
	waitForEmpty();
	fprintf(stderr, "Waited for empty\n");
	if (bEOS) {
	    fprintf(stderr, "Exiting loop\n");
	    break;
	}
    }
    fprintf(stderr, "Buffers emptied\n");
    exit(0);
}
