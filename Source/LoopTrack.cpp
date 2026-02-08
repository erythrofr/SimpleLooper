#include "LoopTrack.h"
#include "DebugLogger.h"

LoopTrack::LoopTrack()
{
}

LoopTrack::~LoopTrack()
{
}

void LoopTrack::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    trackSampleRate = sampleRate;

    // Allocate the buffer for the maximum supported loop time (e.g., 5 mins)
    // We do this here (or in constructor) to AVOID allocation in processBlock
    int totalSamples = static_cast<int>(sampleRate * maxLoopLengthSeconds);
    
    // Assuming stereo for now (2 channels). 
    // If your plugin supports changeable channel counts, pass that in prepareToPlay.
    loopBuffer.setSize(2, totalSamples);
    loopBuffer.clear();
    
    undoBuffer.setSize(2, totalSamples);
    undoBuffer.clear();

    fxCaptureBuffer.setSize(2, totalSamples);
    fxCaptureBuffer.clear();

    clear();
}

void LoopTrack::processBlock(juce::AudioBuffer<float>& outputBuffer, const juce::AudioBuffer<float>& inputBuffer,
                             const juce::AudioBuffer<float>& sidechainBuffer,
                             juce::int64 globalTotalSamples, bool isMasterTrack, int masterLoopLength, bool anySoloActive)
{
    const int numSamples = outputBuffer.getNumSamples();
    State state = currentState.load();

    // If stopped or empty, we generally don't output sound, 
    // providing we aren't currently recording the first pass.
    if (state == State::Stopped || (state == State::Empty && state != State::Recording))
    {
        return;
    }
    
    // Check Solo/Mute logic
    // If ANY solo is active, we are muted UNLESS we are soloed.
    // If NO solo is active, we respect our local mute.
    bool shouldBeSilent = false;
    
    if (anySoloActive)
    {
        if (!isSolo.load()) shouldBeSilent = true;
    }
    else
    {
        if (isMuted.load()) shouldBeSilent = true;
    }
    
    // Force mute for playback if needed
    // (We pass a "effectiveMuted" flag to helpers or handle it there? 
    //  Our helpers check isMuted.load(). We should override that.)

    // Auto-finish Fixed Length Recording (Slave Tracks)
    if (state == State::Recording && !isMasterTrack)
    {
        // Simple check: if we exceed the loop length, we switch to playing
        // S'assurer que le calcul est correct avec des floats
        float mult = targetMultiplier;
        if (mult < 1.0f / 64.0f) mult = 1.0f / 64.0f;
        if (mult > 64.0f) mult = 64.0f;
        
        int targetLen = static_cast<int>((float)masterLoopLength * mult);
        if (targetLen < 1) targetLen = 1; // Sécurité
        
        if (recordedSamplesCurrent >= targetLen)
        {
            // IMPORTANT: Définir la longueur de loop AVANT de passer en Playing
            loopLengthSamples = targetLen;
            
            LOG("SLAVE REC FINISHED | recorded=" + juce::String(recordedSamplesCurrent) + 
                " loopLen=" + juce::String(loopLengthSamples) + 
                " mult=" + juce::String(mult) + 
                " offset=" + juce::String(recordingStartOffset));
            
            setPlaying();
            state = State::Playing;
            
            // NOTE: La synchronisation se fait maintenant automatiquement dans la section Playing
            // grâce au calcul avec recordingStartOffset
        }
    }

    int writePos = 0;
    int readPos = 0;
    int currentLoopLength = 0;

    if (isMasterTrack)
    {
        if (state == State::Recording)
        {
             // Linear recording
             writePos = playbackPosition;
             currentLoopLength = loopBuffer.getNumSamples(); // Prevent overflow
        }
        else
        {
             // Master Playing/Overdubbing - use global transport provided by Processor
             if (loopLengthSamples > 0)
             {
                 readPos = globalTotalSamples % loopLengthSamples;
                 writePos = readPos;
                 currentLoopLength = loopLengthSamples;
             }
        }
    }
    else
    {
        // Slave Track
        if (state == State::Recording)
        {
             // IMPORTANT: Enregistrement d'une track slave
             // On enregistre linéairement dans le buffer depuis position 0
             // MAIS on mémorise à quelle position du cycle master on a commencé
             
             // Si c'est le premier sample enregistré (recordedSamplesCurrent == 0),
             // on capture la position de départ dans le cycle master
              if (recordedSamplesCurrent == 0 && masterLoopLength > 0)
              {
                  // Calculer la position actuelle dans le cycle du master
                  recordingStartOffset = static_cast<int>(globalTotalSamples % masterLoopLength);
                  recordingStartGlobalSample = globalTotalSamples;
                  
                  LOG("SLAVE REC START | offset=" + juce::String(recordingStartOffset) + 
                      " globalSample=" + juce::String((juce::int64)globalTotalSamples) +
                      " masterLen=" + juce::String(masterLoopLength) + 
                      " targetMult=" + juce::String(targetMultiplier));
              }
             
             // Enregistrer linéairement depuis position 0 dans notre buffer
             writePos = recordedSamplesCurrent;
             currentLoopLength = loopBuffer.getNumSamples(); 
             
             // Calculer la longueur cible avec le multiplicateur
             float mult = targetMultiplier;
             if (mult < 1.0f / 64.0f) mult = 1.0f / 64.0f;
             if (mult > 64.0f) mult = 64.0f;
             int targetLen = static_cast<int>((float)masterLoopLength * mult);
             if (targetLen < 1) targetLen = 1;
             
             if (loopLengthSamples != targetLen)
                 loopLengthSamples = targetLen;
        }
        else
        {
              // Slave Playing/Overdubbing
              // SYNCHRONISATION : utiliser le temps absolu écoulé depuis le début de l'enregistrement
              // Cela fonctionne pour les loops plus courtes ET plus longues que le master
              
              if (loopLengthSamples > 0)
              {
                  juce::int64 elapsed = globalTotalSamples - recordingStartGlobalSample;
                  if (elapsed < 0) elapsed = 0;
                  readPos = static_cast<int>(elapsed % loopLengthSamples);
                  writePos = readPos;
                  currentLoopLength = loopLengthSamples;
              }
              else
              {
                  currentLoopLength = masterLoopLength; // fallback
              }
        }
    }

    if (anySoloActive)
    {
        if (!isSolo.load()) shouldBeSilent = true;
    }
    else
    {
        if (isMuted.load()) shouldBeSilent = true;
    }

    // Apply any pending progressive buffer replacement (playhead-first)
    if (mReplace.active)
        processReplaceChunk(readPos, numSamples);

    switch (state)
    {
        case State::Recording:
            handleRecording(inputBuffer, numSamples, writePos);
            
            if (isMasterTrack)
                 playbackPosition += numSamples;
            else
                 recordedSamplesCurrent += numSamples;
            break;

        case State::Playing:
            // Continuously capture sidechain into staging buffer for later one-shot replace
            if (loopLengthSamples > 0)
                captureSidechain(sidechainBuffer, numSamples, readPos, currentLoopLength);

            // We must update position even if silent
            if (shouldBeSilent)
            {
                 handlePlayback(outputBuffer, numSamples, readPos, currentLoopLength, true);
            }
            else
            {
                handlePlayback(outputBuffer, numSamples, readPos, currentLoopLength, false);
            }
            break;

        case State::Overdubbing:
             // Continuously capture sidechain into staging buffer for later one-shot replace
             if (loopLengthSamples > 0)
                 captureSidechain(sidechainBuffer, numSamples, readPos, currentLoopLength);

             handleOverdub(outputBuffer, inputBuffer, numSamples, readPos, currentLoopLength, shouldBeSilent);
            break;

        default:
            break;
    }
}

void LoopTrack::clear()
{
    currentState.store(State::Empty);
    loopLengthSamples = 0;
    playbackPosition = 0;
    recordedSamplesCurrent = 0;
    loopBuffer.clear();
    undoLoopLengthSamples = 0;
    hasUndo = false;
    
    // IMPORTANT : Réinitialiser le targetMultiplier à 1.0 (valeur par défaut)
    targetMultiplier = 1.0f;
    
    // IMPORTANT : Réinitialiser l'offset de synchronisation
    recordingStartOffset = 0;
    recordingStartGlobalSample = 0;
    fxCaptureSamplesWritten = 0;

    // Cancel any in-flight progressive replace
    mReplace.active = false;
    mReplace.source = nullptr;
}

//==============================================================================
// Operations

void LoopTrack::saveUndo()
{
    // Back up the current loop buffer and length
    int len = loopLengthSamples;
    if (len > 0)
    {
        // We only copy the valid part
        for (int ch = 0; ch < undoBuffer.getNumChannels(); ++ch)
            undoBuffer.copyFrom(ch, 0, loopBuffer, ch, 0, len);
        
        undoLoopLengthSamples = len;
        hasUndo = true;
    }
}

void LoopTrack::performUndo()
{
    if (hasUndo && undoLoopLengthSamples > 0)
    {
        // Stop any active recording/overdubbing first
        if (currentState.load() == State::Recording || currentState.load() == State::Overdubbing)
            setPlaying();

        // Swap Logic (Undo <-> Current)
        // This effectively makes "Undo" toggle between two states (Undo/Redo)
        
        // 1. Temp copy current to temp buffer? 
        // Or simplified: We just want to restore the OLD state.
        // But the user asked: "si on rappuie sur undo ça fait un redo"
        // So we swap.
        
        // We need a temp buffer or just swap content if both are same size.
        // Since undoBuffer and loopBuffer are full size allocated, we can just swap content pointer? 
        // No, AudioBuffer doesn't own memory like that easily without pointer swap.
        // Let's do a deep copy swap for safety or use a temp cache if we had one.
        // For simplicity: We will just COPY undo -> loop, and SAVE loop -> undo BEFORE that.
        
        // A full swap is better.
        // We do it manually with a temp buffer would be slow.
        // Optimized:
        // We can just swap the data via copying.
        
        int currentLen = loopLengthSamples;
        int restoredLen = undoLoopLengthSamples;
        
        // 1. Copy Current -> Temp (Using the high part of undo buffer? Risky)
        // Let's assume we just Restore for now, implementing Swap for Redo requires 3rd buffer or smart swaping.
        // "Undo" typically implies "Revert". "Redo" implies "Revert the Revert".
        // So swapping is the correct logic.
        
        // We can use a trick: Swap the contents.
        // juce::AudioBuffer doesn't support move assignment easily to swap internal arrays exposed publicly.
        // But we can copy.
        
        // Limit: This is running on Message Thread (button click) usually.
        // Copying 5 minutes of audio double precision... might take a few milliseconds.
        // Let's protect against Audio Thread access? 
        // In a "Simple Looper", we might accept a glitch or use a SpinLock.
        
        // SWAP IMPLEMENTATION:
        
        // 1. Read Undo to Temp? No.
        // We'll proceed with simple copy for now, assuming user invoked Rec/Overdub -> SaveUndo.
        // If we want Undo/Redo, we should have swapped.
        
        // To support "Redo", we treat the current state as the new "Undo" state before restoring.
        
        // A -> B (Undo saves A)
        // Undo Called:
        // Temp = B
        // B = A
        // A = Temp
        
        // We need a temporary buffer. We can allocate one locally on UI thread.
        // Or just use the unsued part of the buffer if we know we have space? (Risky)
        
        // Let's just do a direct swap loop.
        int maxLen = juce::jmax(currentLen, restoredLen);
        for (int ch = 0; ch < loopBuffer.getNumChannels(); ++ch)
        {
            auto* d1 = loopBuffer.getWritePointer(ch);
            auto* d2 = undoBuffer.getWritePointer(ch);
            for (int i = 0; i < maxLen; ++i)
            {
                std::swap(d1[i], d2[i]);
            }
        }
        
        loopLengthSamples = restoredLen;
        undoLoopLengthSamples = currentLen;
        
        // hasUndo remains true (to allow Redo)
    }
}

void LoopTrack::multiplyLoop()
{
    if (loopLengthSamples <= 0) 
    {
        // Pre-recording: increase target length
        targetMultiplier = std::min(64.0f, targetMultiplier * 2.0f);
        return;
    }
    
    // Check bounds
    if (loopLengthSamples * 2 > loopBuffer.getNumSamples()) return;
    
    saveUndo();
    
    // Copy [0..len] to [len..2*len]
    for (int ch = 0; ch < loopBuffer.getNumChannels(); ++ch)
    {
        loopBuffer.copyFrom(ch, loopLengthSamples, loopBuffer, ch, 0, loopLengthSamples);
    }
    
    loopLengthSamples *= 2;
}

void LoopTrack::divideLoop()
{
    if (loopLengthSamples <= 0)
    {
        // Pre-recording: decrease target length
        targetMultiplier = std::max(1.0f / 64.0f, targetMultiplier / 2.0f);
        return;
    }
    
    // Minimum ~256 samples to avoid degenerate loops
    if (loopLengthSamples / 2 < 256) return;
    
    saveUndo();
    
    loopLengthSamples /= 2;
}

//==============================================================================
// State Setters

void LoopTrack::setRecording()
{
    // Can only start fresh recording if we are empty or choose to overwrite.
    // For this basic implementation, we assume we can only 'Record' from Empty.
    // Use 'Overdub' to add to existing.
    if (currentState.load() == State::Empty)
    {
        playbackPosition = 0;
        loopLengthSamples = 0; // Reset length
        recordedSamplesCurrent = 0;
        
        LOG("LoopTrack: RECORDING started | targetMult=" + juce::String(targetMultiplier));
        
        currentState.store(State::Recording);
    }
}

void LoopTrack::setOverdubbing()
{
    if (loopLengthSamples > 0)
    {
        // Save state before overdubbing starts
        // CHECK: If we are already overdubbing, don't save again?
        // Usually we save when entering the state.
        if (currentState.load() != State::Overdubbing)
        {
            saveUndo();
        }

        currentState.store(State::Overdubbing);
    }
}

void LoopTrack::setPlaying()
{
    // If we were recording, this transition DEFINES the loop length
    if (currentState.load() == State::Recording)
    {
        // If master track (linear recording), use playbackPosition
        if (playbackPosition > 0)
        {
            loopLengthSamples = playbackPosition;
            LOG("LoopTrack: MASTER REC->PLAY | len=" + juce::String(loopLengthSamples) + 
                " playbackPos=" + juce::String(playbackPosition));
        }
        else if (recordedSamplesCurrent > 0)
        {
            // SLAVE TRACK Logic
            // If we recorded as a slave, the loop length is handled externally or implicitly.
            // We ensure loopLengthSamples is valid if it wasn't already set.
            // Note: processBlock usually sets this for slaves, but just in case:
            if (loopLengthSamples == 0)
                 loopLengthSamples = recordedSamplesCurrent; // Fallback
            
            LOG("LoopTrack: SLAVE REC->PLAY | len=" + juce::String(loopLengthSamples) + 
                " recordedSamples=" + juce::String(recordedSamplesCurrent) + 
                " targetMult=" + juce::String(targetMultiplier));
        }
        
        playbackPosition = 0; // Reset to start for playback
        LOG("LoopTrack: Playback position RESET to 0");
    }

    if (loopLengthSamples > 0)
    {
        // Smooth the loop boundary to eliminate the click when recording stops
        applyCrossfade(loopBuffer, loopLengthSamples, 128);

        currentState.store(State::Playing);
        LOG("LoopTrack: State = PLAYING");
    }
    else
    {
        LOG_ERROR("LoopTrack: Cannot play - loopLength is 0!");
    }
}

void LoopTrack::stop()
{
    // If we press stop while recording, we define the loop length but go silent
    if (currentState.load() == State::Recording)
    {
        loopLengthSamples = playbackPosition;
        playbackPosition = 0;
    }
    
    if (loopLengthSamples > 0)
    {
        currentState.store(State::Stopped);
        playbackPosition = 0; // Optional: rewind on stop
    }
}

void LoopTrack::setLoopFromMix(const juce::AudioBuffer<float>& mixedBuffer, int length, int startOffset, juce::int64 startGlobalSample)
{
    if (length <= 0 || length > loopBuffer.getNumSamples()) return;

    saveUndo();

    loopBuffer.clear();
    for (int ch = 0; ch < juce::jmin(loopBuffer.getNumChannels(), mixedBuffer.getNumChannels()); ++ch)
        loopBuffer.copyFrom(ch, 0, mixedBuffer, ch, 0, length);

    loopLengthSamples = length;
    playbackPosition = 0;
    recordedSamplesCurrent = length;
    recordingStartOffset = startOffset;
    recordingStartGlobalSample = startGlobalSample;
    currentState.store(State::Playing);

    LOG("LoopTrack::setLoopFromMix | len=" + juce::String(length) + " offset=" + juce::String(startOffset) + " globalSample=" + juce::String(startGlobalSample));
}

void LoopTrack::overdubFromBuffer(const juce::AudioBuffer<float>& inputBuffer, int inputLength, juce::int64 inputStartGlobalSample)
{
    if (loopLengthSamples <= 0 || inputLength <= 0) return;

    saveUndo();

    // Compute where in our loop the input audio starts
    juce::int64 elapsed = inputStartGlobalSample - recordingStartGlobalSample;
    if (elapsed < 0) elapsed = 0;
    int writeStart = static_cast<int>(elapsed % loopLengthSamples);

    // Add input audio on top of existing loop buffer, with wrapping
    int numCh = juce::jmin(loopBuffer.getNumChannels(), inputBuffer.getNumChannels());
    for (int ch = 0; ch < numCh; ++ch)
    {
        int remaining = inputLength;
        int srcOffset = 0;
        int dstPos = writeStart;

        while (remaining > 0)
        {
            int toEnd = loopLengthSamples - dstPos;
            int chunk = juce::jmin(remaining, toEnd);
            loopBuffer.addFrom(ch, dstPos, inputBuffer, ch, srcOffset, chunk);
            srcOffset += chunk;
            dstPos += chunk;
            if (dstPos >= loopLengthSamples) dstPos = 0;
            remaining -= chunk;
        }
    }

    LOG("LoopTrack::overdubFromBuffer | inputLen=" + juce::String(inputLength) +
        " writeStart=" + juce::String(writeStart) + " loopLen=" + juce::String(loopLengthSamples));
}

//==============================================================================
// Audio Processing Implementation

void LoopTrack::handleRecording(const juce::AudioBuffer<float>& inputBuffer, int numSamples, int startWritePos)
{
    // In 'Recording' state, we are defining the loop length.
    // We just write linearly into the buffer.
    
    // Safety check: don't overflow the max allocated buffer
    if (startWritePos + numSamples >= loopBuffer.getNumSamples())
    {
        // Handle buffer overflow (auto-finish loop or stop)
        setPlaying(); 
        // handlePlayback might fail if we don't have loopLength set, but setPlaying sets it if we were linear recording...
        // But for slave tracks, we shouldn't hit this unless the master loop is huge.
        return;
    }

    // Copy input to loop buffer
    for (int channel = 0; channel < juce::jmin(inputBuffer.getNumChannels(), loopBuffer.getNumChannels()); ++channel)
    {
        loopBuffer.copyFrom(channel, startWritePos, inputBuffer, channel, 0, numSamples);
    }
}

void LoopTrack::handlePlayback(juce::AudioBuffer<float>& outputBuffer, int numSamples, int startReadPos, int loopEndRes, bool shouldBeSilent)
{
    if (loopEndRes <= 0)
        return;
        
    // If shouldBeSilent is true, we just don't add to output, but we assume the caller handles position tracking?
    // Wait, handlePlayback computes position locally but doesn't store it back if we pass 'readPos' by value.
    // 'readPos' is likely currentPlaybackPos (Global) or local.
    // In processBlock, we pass 'readPos'.
    // If we are master, we update Global Playback Position? No, PluginProcessor updates Global.
    // processBlock updates playbackPosition.
    
    if (shouldBeSilent)
        return;

    if (isMuted.load())
        return;

    float currentGain = gain.load();

    // During progressive replace, read from the source buffer (complete correct audio)
    // so there's no discontinuity between replaced and unreplaced regions.
    const juce::AudioBuffer<float>& readBuf =
        (mReplace.active && mReplace.source) ? *mReplace.source : loopBuffer;

    // Circular buffer read
    int samplesToDo = numSamples;
    int currentOutputOffset = 0;
    int localReadPos = startReadPos;

    while (samplesToDo > 0)
    {
        // Safety wrap
        if (localReadPos >= loopEndRes)
            localReadPos = 0;

        int samplesToEnd = loopEndRes - localReadPos;
        int chunk = juce::jmin(samplesToDo, samplesToEnd);
        
        if (chunk <= 0)
             break;

        // Add loop content to main output (Summing)
        for (int channel = 0; channel < juce::jmin(outputBuffer.getNumChannels(), readBuf.getNumChannels()); ++channel)
        {
            outputBuffer.addFrom(channel, currentOutputOffset, readBuf, channel, localReadPos, chunk, currentGain);
        }

        currentOutputOffset += chunk;
        localReadPos += chunk;
        samplesToDo -= chunk;

        // Wrap around
        if (localReadPos >= loopEndRes)
            localReadPos = 0;
    }
}

void LoopTrack::captureSidechain(const juce::AudioBuffer<float>& sidechainBuffer, int numSamples, int startWritePos, int loopEndRes)
{
    if (loopEndRes <= 0) return;

    int samplesToDo = numSamples;
    int srcOffset = 0;
    int localPos = startWritePos;

    while (samplesToDo > 0)
    {
        if (localPos >= loopEndRes)
            localPos = 0;

        int samplesToEnd = loopEndRes - localPos;
        int chunk = juce::jmin(samplesToDo, samplesToEnd);
        if (chunk <= 0) break;

        for (int ch = 0; ch < juce::jmin(sidechainBuffer.getNumChannels(), fxCaptureBuffer.getNumChannels()); ++ch)
        {
            fxCaptureBuffer.copyFrom(ch, localPos, sidechainBuffer, ch, srcOffset, chunk);
        }

        srcOffset += chunk;
        localPos += chunk;
        samplesToDo -= chunk;
        fxCaptureSamplesWritten += chunk;

        if (localPos >= loopEndRes)
            localPos = 0;
    }
}

void LoopTrack::applyFxReplace()
{
    if (loopLengthSamples <= 0) return;
    if (fxCaptureSamplesWritten < loopLengthSamples) return;

    saveUndo();

    for (int ch = 0; ch < juce::jmin(loopBuffer.getNumChannels(), fxCaptureBuffer.getNumChannels()); ++ch)
    {
        loopBuffer.copyFrom(ch, 0, fxCaptureBuffer, ch, 0, loopLengthSamples);
    }

    fxCaptureSamplesWritten = 0;
    LOG("FX Replace applied | loopLen=" + juce::String(loopLengthSamples));
}

void LoopTrack::handleOverdub(juce::AudioBuffer<float>& outputBuffer, const juce::AudioBuffer<float>& inputBuffer, int numSamples, int startReadPos, int loopEndRes, bool shouldBeSilent)
{
    if (loopEndRes <= 0) return;

    // Overdub = Playback existing + Write new input on top
    
    // Cache atomic values
    bool muted = isMuted.load();
    float currentGain = gain.load();
    
    // During progressive replace, read from the source buffer
    const juce::AudioBuffer<float>& readBuf =
        (mReplace.active && mReplace.source) ? *mReplace.source : loopBuffer;
    
    // If effective silence is forced (e.g. valid loop but soloed out), we treat as muted output
    if (shouldBeSilent) muted = true;

    int samplesToDo = numSamples;
    int currentOffset = 0;
    int localPos = startReadPos;

    while (samplesToDo > 0)
    {
        // Safety wrap
        if (localPos >= loopEndRes)
            localPos = 0;

        int samplesToEnd = loopEndRes - localPos;
        int chunk = juce::jmin(samplesToDo, samplesToEnd);
        
        if (chunk <= 0) break;

        for (int channel = 0; channel < juce::jmin(outputBuffer.getNumChannels(), loopBuffer.getNumChannels()); ++channel)
        {
            // 1. Output the existing loop audio (if not muted)
            if (!muted)
            {
                outputBuffer.addFrom(channel, currentOffset, readBuf, channel, localPos, chunk, currentGain);
            }

            // 2. Input -> Add to Storage (Constructive interference / Summing)
            loopBuffer.addFrom(channel, localPos, inputBuffer, channel, currentOffset, chunk);
        }

        currentOffset += chunk;
        localPos += chunk;
        samplesToDo -= chunk;

        if (localPos >= loopEndRes)
            localPos = 0;
    }
}

void LoopTrack::beginProgressiveReplace(const juce::AudioBuffer<float>* source, int length,
                                         int startOffset, juce::int64 startGlobal)
{
    if (!source || length <= 0 || length > loopBuffer.getNumSamples()) return;

    saveUndo();

    mReplace.source    = source;
    mReplace.length    = length;
    mReplace.cursor    = 0;
    mReplace.remaining = length;
    mReplace.active    = true;

    // Update metadata immediately so playback wraps at the new length
    loopLengthSamples          = length;
    playbackPosition           = 0;
    recordedSamplesCurrent     = length;
    recordingStartOffset       = startOffset;
    recordingStartGlobalSample = startGlobal;

    if (currentState.load() == State::Empty)
        currentState.store(State::Playing);

    LOG("beginProgressiveReplace | len=" + juce::String(length));
}

void LoopTrack::processReplaceChunk(int playheadPos, int blockSize)
{
    if (!mReplace.active || !mReplace.source) return;

    int numCh = juce::jmin(loopBuffer.getNumChannels(), mReplace.source->getNumChannels());
    int len   = mReplace.length;

    auto copyRegion = [&](int startPos, int count) {
        int pos = startPos;
        int rem = count;
        while (rem > 0) {
            if (pos >= len) pos -= len;
            int toEnd = len - pos;
            int chunk  = juce::jmin(rem, toEnd);
            for (int ch = 0; ch < numCh; ++ch)
                loopBuffer.copyFrom(ch, pos, *mReplace.source, ch, pos, chunk);
            pos += chunk;
            rem -= chunk;
        }
    };

    // Sequential fill only — safe because playback reads from mReplace.source,
    // not from loopBuffer. No read/write conflict possible.
    int budget = juce::jmin(blockSize * 16, mReplace.remaining);
    if (budget > 0)
    {
        copyRegion(mReplace.cursor, budget);
        mReplace.cursor    = (mReplace.cursor + budget) % len;
        mReplace.remaining -= budget;
    }

    if (mReplace.remaining <= 0)
    {
        mReplace.active = false;
        mReplace.source = nullptr;
        LOG("Progressive replace complete");
    }
}

void LoopTrack::applyCrossfade(juce::AudioBuffer<float>& buffer, int loopLength, int fadeSamples)
{
    if (loopLength <= 0 || fadeSamples <= 0) return;
    fadeSamples = juce::jmin(fadeSamples, loopLength / 2);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);

        // Crossfade: blend the end of the loop into the beginning
        // so that when the loop wraps around there is no discontinuity.
        for (int i = 0; i < fadeSamples; ++i)
        {
            float fadeIn  = (float)i / (float)fadeSamples;           // 0 ? 1
            float fadeOut = 1.0f - fadeIn;                           // 1 ? 0

            float headSample = data[i];
            float tailSample = data[loopLength - fadeSamples + i];

            // Blend: beginning fades in from tail, tail fades out into beginning
            data[i]                          = headSample * fadeIn + tailSample * fadeOut;
            data[loopLength - fadeSamples + i] = tailSample * fadeOut + headSample * fadeIn;
        }
    }
}
