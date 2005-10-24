/*
 * Oreka -- A media capture and retrieval platform
 * 
 * Copyright (C) 2005, orecx LLC
 *
 * http://www.orecx.com
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 * Please refer to http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "CapturePluginProxy.h"
#include "ace/OS_NS_dirent.h"
#include "ace/OS_NS_string.h"
#include "ConfigManager.h"
#include "CapturePort.h"

CapturePluginProxy::CapturePluginProxy()
{
	m_configureFunction = NULL;
	m_registerCallBacksFunction = NULL;
	m_initializeFunction = NULL;
	m_runFunction = NULL;
	m_startCaptureFunction = NULL;
	m_stopCaptureFunction = NULL;

	m_loaded = false;
}

bool CapturePluginProxy::Initialize()
{
	// Get the desired capture plugin from the config file, or else, use the first dll encountered.
	CStdString pluginDirectory = CONFIG.m_capturePluginPath + "/";
	CStdString pluginPath;
	if (!CONFIG.m_capturePlugin.IsEmpty())
	{
		// A specific plugin was specified in the config file
		pluginPath = pluginDirectory + CONFIG.m_capturePlugin;
	}
	else
	{
		// No plugin specified, find the first one in the plugin directory
		ACE_DIR* dir = ACE_OS::opendir((PCSTR)pluginDirectory);
		if (!dir)
		{
			LOG4CXX_ERROR(LOG.rootLog, CStdString("Capture plugin directory could not be found:" + pluginDirectory));
		}
		else
		{
			dirent* dirEntry = NULL;
			bool found = false;
			bool done = false;
			while(!found && !done)
			{	
				dirEntry = ACE_OS::readdir(dir);
				if(dirEntry)
				{
					if (ACE_OS::strstr(dirEntry->d_name, ".dll"))
					{
						found = true;
						done = true;
						pluginPath = pluginDirectory + dirEntry->d_name;
					}
				}
				else
				{
					done = true;
				}
			}
			ACE_OS::closedir(dir);
		}
	}
	if (!pluginPath.IsEmpty())
	{
		m_dll.open((PCSTR)pluginPath);
		ACE_TCHAR* error = m_dll.error();
		if(error)
		{
			LOG4CXX_ERROR(LOG.rootLog, CStdString("Failed to load the following plugin: ") + pluginPath);
		}
		else
		{
			// Ok, the dll has been successfully loaded
			RegisterCallBacksFunction registerCallBacks;
			registerCallBacks = (RegisterCallBacksFunction)m_dll.symbol("RegisterCallBacks");
			registerCallBacks(AudioChunkCallBack, CaptureEventCallBack, LogManagerSingleton::instance());

			m_configureFunction = (ConfigureFunction)m_dll.symbol("Configure");
			if (m_configureFunction)
			{
				ConfigManagerSingleton::instance()->AddConfigureFunction(m_configureFunction);

				m_initializeFunction = (InitializeFunction)m_dll.symbol("Initialize");
				if (m_initializeFunction)
				{
					m_initializeFunction();

					m_runFunction = (RunFunction)m_dll.symbol("Run");
					if (m_runFunction)
					{
						m_startCaptureFunction = (StartCaptureFunction)m_dll.symbol("StartCapture");
						if (m_startCaptureFunction)
						{
							m_stopCaptureFunction = (StopCaptureFunction)m_dll.symbol("StopCapture");
							if (m_stopCaptureFunction)
							{
								m_loaded = true;
							}
							else
							{
								LOG4CXX_ERROR(LOG.rootLog, CStdString("Could not find StopCapture function in ") + pluginPath);
							}
						}
						else
						{
							LOG4CXX_ERROR(LOG.rootLog, CStdString("Could not find StartCapture function in ") + pluginPath);
						}
					}
					else
					{
						LOG4CXX_ERROR(LOG.rootLog, CStdString("Could not find Run function in ") + pluginPath);
					}
				}
				else
				{
					LOG4CXX_ERROR(LOG.rootLog, CStdString("Could not find Initialize function in ") + pluginPath);
				}
			}
			else
			{
				LOG4CXX_ERROR(LOG.rootLog, CStdString("Could not find Configure function in ") + pluginPath);
			}
		}
	}
	else
	{
		LOG4CXX_ERROR(LOG.rootLog, CStdString("Failed to find any capture plugin in: ") + pluginDirectory);
	}

	return m_loaded;
}

void CapturePluginProxy::Run()
{
	m_runFunction();
}

void CapturePluginProxy::StartCapture(CStdString& capturePort)
{
	if(m_loaded)
	{
		m_startCaptureFunction(capturePort);
	}
	else
	{
		throw(CStdString("StartCapture: Capture plugin not yet loaded"));
	}
}

void CapturePluginProxy::StopCapture(CStdString& capturePort)
{
	if(m_loaded)
	{
		m_stopCaptureFunction(capturePort);
	}
	else
	{
		throw(CStdString("StopCapture: Capture plugin not yet loaded"));
	}
}

void __CDECL__  CapturePluginProxy::AudioChunkCallBack(AudioChunkRef chunkRef, CStdString& capturePort, bool remote)
{
	// find the right port and give it the audio chunk
	CapturePortRef portRef = CapturePortsSingleton::instance()->AddAndReturnPort(capturePort);
	portRef->AddAudioChunk(chunkRef, remote);
}

void __CDECL__ CapturePluginProxy::CaptureEventCallBack(CaptureEventRef eventRef, CStdString& capturePort)
{
	if(CONFIG.m_vad || CONFIG.m_audioSegmentation)
	{
		if (eventRef->m_type == CaptureEvent::EtStart || eventRef->m_type == CaptureEvent::EtStop)
		{
			LOG4CXX_ERROR(LOG.portLog, "#" + capturePort + ": received start or stop while in VAD or audio segmentation mode");
		}
	}
	else
	{
		// find the right port and give it the event
		CapturePortRef portRef = CapturePortsSingleton::instance()->AddAndReturnPort(capturePort);
		portRef->AddCaptureEvent(eventRef);
	}
}
