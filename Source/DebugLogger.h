#pragma once

#include <JuceHeader.h>
#include <fstream>
#include <sstream>
#include <memory>

// ACTIVER/DÉSACTIVER LE LOGGER
#define ENABLE_DEBUG_LOGGER 0  // 1 = activé, 0 = désactivé (DÉSACTIVÉ pour éviter les crashes)

#if ENABLE_DEBUG_LOGGER

/**
 * Singleton Logger pour tracer tous les événements du Looper
 * Écrit dans un fichier avec timestamps pour débogage
 */
class DebugLogger
{
public:
    static DebugLogger& getInstance()
    {
        static DebugLogger instance;
        return instance;
    }

    // Log un événement simple
    void log(const juce::String& message)
    {
        if (!isEnabled) return;
        
        auto timestamp = juce::Time::getCurrentTime();
        auto timestampStr = timestamp.formatted("%H:%M:%S.%f");
        
        juce::String fullMessage = "[" + timestampStr + "] " + message;
        
        // Thread-safe write
        const juce::ScopedLock lock(logLock);
        
        if (logFile.is_open())
        {
            logFile << fullMessage.toStdString() << std::endl;
            logFile.flush(); // Force write immédiatement
        }
        
        // Aussi dans la console pour debug temps réel
        DBG(fullMessage);
    }
    
    // Log un événement avec track ID
    void logTrack(int trackID, const juce::String& event, const juce::String& details = "")
    {
        juce::String msg = "TRACK " + juce::String(trackID) + ": " + event;
        if (details.isNotEmpty())
            msg += " | " + details;
        log(msg);
    }
    
    // Log un changement d'état
    void logStateChange(int trackID, const juce::String& oldState, const juce::String& newState)
    {
        logTrack(trackID, "STATE CHANGE", oldState + " -> " + newState);
    }
    
    // Log les positions et longueurs
    void logPosition(int trackID, int position, int loopLength, int globalPos = -1)
    {
        juce::String details = "pos=" + juce::String(position) + 
                              " len=" + juce::String(loopLength);
        if (globalPos >= 0)
            details += " global=" + juce::String(globalPos);
        logTrack(trackID, "POSITION", details);
    }
    
    // Log les actions utilisateur (boutons)
    void logButton(const juce::String& buttonName, int trackID = -1)
    {
        juce::String msg = "BUTTON: " + buttonName;
        if (trackID >= 0)
            msg += " (Track " + juce::String(trackID) + ")";
        log(msg);
    }
    
    // Log les valeurs importantes
    void logValue(const juce::String& name, double value)
    {
        log("VALUE: " + name + " = " + juce::String(value, 3));
    }
    
    // Log une erreur ou warning
    void logError(const juce::String& error)
    {
        log("*** ERROR: " + error);
    }
    
    void logWarning(const juce::String& warning)
    {
        log("!!! WARNING: " + warning);
    }
    
    // Séparateurs pour lisibilité
    void logSeparator(const juce::String& title = "")
    {
        if (title.isEmpty())
            log("========================================");
        else
            log("======== " + title + " ========");
    }
    
    // Activer/désactiver le logging
    void setEnabled(bool enabled) { isEnabled = enabled; }
    bool getEnabled() const { return isEnabled; }
    
    // Initialiser explicitement le logger (à appeler après l'initialisation de JUCE)
    void initialize()
    {
        if (isInitialized) return;
        
        try
        {
            // Créer le fichier de log dans le dossier Documents
            auto documentsDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
            auto logFileObj = documentsDir.getChildFile("SimpleLooper_Debug.log");
            logFilePath = logFileObj.getFullPathName();
            
            logFile.open(logFilePath.toStdString(), std::ios::out | std::ios::app);
            
            if (logFile.is_open())
            {
                isInitialized = true;
                logSeparator("NEW SESSION");
                log("Log file: " + logFilePath);
                log("Start time: " + juce::Time::getCurrentTime().toString(true, true));
            }
        }
        catch (...)
        {
            isInitialized = false;
        }
    }
    
    // Fermer et rouvrir le fichier (utile pour vider le buffer)
    void flush()
    {
        const juce::ScopedLock lock(logLock);
        if (logFile.is_open())
            logFile.flush();
    }
    
    // Clear le fichier log
    void clearLog()
    {
        const juce::ScopedLock lock(logLock);
        if (logFile.is_open())
        {
            logFile.close();
            logFile.open(logFilePath.toStdString(), std::ios::out | std::ios::trunc);
            log("=== LOG CLEARED ===");
        }
    }

private:
DebugLogger()
{
    // Ne pas initialiser ici - attendre l'appel explicite à initialize()
    // pour éviter les problèmes avec l'ordre d'initialisation
}
    
~DebugLogger()
{
    if (logFile.is_open())
    {
        try
        {
            log("=== SESSION END ===");
        }
        catch (...) {}
        logFile.close();
    }
}
    
    
// Pas de copie
DebugLogger(const DebugLogger&) = delete;
DebugLogger& operator=(const DebugLogger&) = delete;
    
std::ofstream logFile;
juce::String logFilePath;
juce::CriticalSection logLock;
bool isEnabled = true;
bool isInitialized = false;
};

// Macros pratiques pour logger
#define LOG(msg) DebugLogger::getInstance().log(msg)
#define LOG_TRACK(trackID, event, details) DebugLogger::getInstance().logTrack(trackID, event, details)
#define LOG_STATE(trackID, old, new) DebugLogger::getInstance().logStateChange(trackID, old, new)
#define LOG_POS(trackID, pos, len, global) DebugLogger::getInstance().logPosition(trackID, pos, len, global)
#define LOG_BUTTON(name, trackID) DebugLogger::getInstance().logButton(name, trackID)
#define LOG_VALUE(name, val) DebugLogger::getInstance().logValue(name, val)
#define LOG_ERROR(msg) DebugLogger::getInstance().logError(msg)
#define LOG_WARNING(msg) DebugLogger::getInstance().logWarning(msg)
#define LOG_SEP(title) DebugLogger::getInstance().logSeparator(title)

#else

// Logger désactivé - Macros vides qui ne font rien
#define LOG(msg) ((void)0)
#define LOG_TRACK(trackID, event, details) ((void)0)
#define LOG_STATE(trackID, old, new) ((void)0)
#define LOG_POS(trackID, pos, len, global) ((void)0)
#define LOG_BUTTON(name, trackID) ((void)0)
#define LOG_VALUE(name, val) ((void)0)
#define LOG_ERROR(msg) ((void)0)
#define LOG_WARNING(msg) ((void)0)
#define LOG_SEP(title) ((void)0)

// Classe vide pour éviter les erreurs de compilation
class DebugLogger
{
public:
    static DebugLogger& getInstance() { static DebugLogger instance; return instance; }
    void initialize() {}
    void log(const juce::String&) {}
    void setEnabled(bool) {}
};

#endif
