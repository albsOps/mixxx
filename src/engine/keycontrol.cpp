#include <QtDebug>
#include <QPair>

#include "engine/keycontrol.h"

#include "controlobject.h"
#include "controlpushbutton.h"
#include "controlpotmeter.h"
#include "engine/enginebuffer.h"
#include "track/keyutils.h"

static const int kOffsetScaleLockOriginalKey = 0;
static const int kAbsoluteScaleLockCurrentKey = 1;

KeyControl::KeyControl(QString group,
                       ConfigObject<ConfigValue>* pConfig)
        : EngineControl(group, pConfig),
          m_iPitchAndKeylockMode(kOffsetScaleLockOriginalKey) {
    struct PitchTempoRatio pitchRateInfo;
    pitchRateInfo.pitchRatio = 1.0;
    pitchRateInfo.tempoRatio = 1.0;
    pitchRateInfo.pitchTweakRatio = 1.0;
    pitchRateInfo.keylock = false;
    m_pitchRateInfo.setValue(pitchRateInfo);

    // pitch knob in semitones [4.7 ct] allowOutOfBounds = true;
    m_pPitch = new ControlPotmeter(ConfigKey(group, "pitch"), -3.0, 3.0, true);
    // Course adjust by full step.
    m_pPitch->setStepCount(6);
    // Fine adjust with semitone / 5 = 20 ct;.
    m_pPitch->setSmallStepCount(30);

    connect(m_pPitch, SIGNAL(valueChanged(double)),
            this, SLOT(slotPitchChanged(double)),
            Qt::DirectConnection);

    m_pButtonSyncKey = new ControlPushButton(ConfigKey(group, "sync_key"));
    connect(m_pButtonSyncKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotSyncKey(double)),
            Qt::DirectConnection);

    m_pButtonResetKey = new ControlPushButton(ConfigKey(group, "reset_key"));
    connect(m_pButtonResetKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotResetKey(double)),
            Qt::DirectConnection);

    m_pFileKey = new ControlObject(ConfigKey(group, "file_key"));
    connect(m_pFileKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotFileKeyChanged(double)),
            Qt::DirectConnection);

    m_pEngineKey = new ControlObject(ConfigKey(group, "key"));
    connect(m_pEngineKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotSetEngineKey(double)),
            Qt::DirectConnection);

    m_pEngineKeyDistance = new ControlPotmeter(ConfigKey(group, "visual_key_distance"),
                                               -0.5, 0.5);
    connect(m_pEngineKeyDistance, SIGNAL(valueChanged(double)),
            this, SLOT(slotSetEngineKeyDistance(double)),
            Qt::DirectConnection);


    m_pitchAndKeylockMode = new ControlPushButton(ConfigKey(group, "pitchAndKeylockMode"));
    m_pitchAndKeylockMode->setButtonMode(ControlPushButton::TOGGLE);
    connect(m_pitchAndKeylockMode, SIGNAL(valueChanged(double)),
            this, SLOT(slotPitchAndKeylockModeChanged(double)),
            Qt::DirectConnection);


    // In case of vinyl control "rate" is a filtered mean value for display
    m_pRateSlider = ControlObject::getControl(ConfigKey(group, "rate"));
    connect(m_pRateSlider, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateSlider, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pRateRange = ControlObject::getControl(ConfigKey(group, "rateRange"));
    connect(m_pRateRange, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateRange, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pRateDir = ControlObject::getControl(ConfigKey(group, "rate_dir"));
    connect(m_pRateDir, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateDir, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pKeylock = ControlObject::getControl(ConfigKey(group, "keylock"));
    connect(m_pKeylock, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pKeylock, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
}

KeyControl::~KeyControl() {
    delete m_pPitch;
    delete m_pButtonSyncKey;
    delete m_pButtonResetKey;
    delete m_pFileKey;
    delete m_pEngineKey;
    delete m_pEngineKeyDistance;
}

KeyControl::PitchTempoRatio KeyControl::getPitchTempoRatio() const {
    return m_pitchRateInfo.getValue();
}

double KeyControl::getKey() {
    return m_pEngineKey->get();
}

void KeyControl::slotRateChanged() {
    //qDebug() << "KeyControl::slotRateChanged 1" << m_pitchRatio << m_speedSliderPitchRatio;

    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();

    // If rate is not 1.0 then we have to try and calculate the octave change
    // caused by it.

    pitchRateInfo.tempoRatio = 1.0 + m_pRateDir->get() * m_pRateRange->get() * m_pRateSlider->get();

    if (pitchRateInfo.tempoRatio == 0) {
        // no transport, no pitch
        // so we can skip pitch calculation
        m_pitchRateInfo.setValue(pitchRateInfo);
        return;
    }


    // |-----------------------|-----------------|
    //   SpeedSliderPitchRatio   pitchTweakRatio
    //
    // |-----------------------------------------|
    //   m_pitchRatio
    //
    //                         |-----------------|
    //                           m_pPitch (0)
    //
    // |-----------------------------------------|
    //   m_pPitch (1)

    double speedSliderPitchRatio = pitchRateInfo.pitchRatio / pitchRateInfo.pitchTweakRatio;

    if (m_pKeylock->toBool()) {
        if (!pitchRateInfo.keylock) {
            // Enabling Keylock
            if (m_iPitchAndKeylockMode == kAbsoluteScaleLockCurrentKey) {
                // Lock at current pitch
                speedSliderPitchRatio = pitchRateInfo.tempoRatio;
            } else {
                // kOffsetScaleLockOriginalKey
                // Lock at original track pitch
                speedSliderPitchRatio = 1.0;
            }
            pitchRateInfo.keylock = true;
        }
    } else {
        // !bKeylock
        if (pitchRateInfo.keylock) {
            // Disabling Keylock
            if (m_iPitchAndKeylockMode == kAbsoluteScaleLockCurrentKey) {
                // reset to linear pitch
                pitchRateInfo.pitchTweakRatio = 1.0;
                // For not resetting to linear pitch:
                // Adopt speedPitchRatio change as pitchTweakRatio
                //pitchRateInfo.pitchTweakRatio *= (m_speedSliderPitchRatio / pitchRateInfo.tempoRatio);
            }
            pitchRateInfo.keylock = false;
        }
        speedSliderPitchRatio = pitchRateInfo.tempoRatio;
    }

    pitchRateInfo.pitchRatio = pitchRateInfo.pitchTweakRatio * speedSliderPitchRatio;
    pitchRateInfo.pitchTweakRatio = pitchRateInfo.pitchRatio / speedSliderPitchRatio;

    double pitchOctaves = KeyUtils::powerOf2ToOctaveChange(pitchRateInfo.pitchRatio);
    double dFileKey = m_pFileKey->get();
    updateKeyCOs(dFileKey, pitchOctaves);

    if (m_iPitchAndKeylockMode == kAbsoluteScaleLockCurrentKey) {
        // Pitch scale is always 0 at original pitch
        m_pPitch->set(pitchOctaves * 12);
    }

    // store so that the entire results are available to the engine at once
    m_pitchRateInfo.setValue(pitchRateInfo);

    // qDebug() << "KeyControl::slotRateChanged 2" << m_pitchRatio << m_speedSliderPitchRatio;
}

void KeyControl::slotPitchAndKeylockModeChanged(double value) {
    //qDebug() << "KeyControl::slotPitchAndKeylockModeChanged 1" << m_pitchRatio << m_speedSliderPitchRatio;

    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();

    if (value == 0.0 && m_iPitchAndKeylockMode == kAbsoluteScaleLockCurrentKey) {
        // absolute mode to offset mode
        if (pitchRateInfo.keylock) {
            pitchRateInfo.pitchTweakRatio = pitchRateInfo.pitchRatio;
        }
    }
    m_iPitchAndKeylockMode = (int)value;
    slotRateChanged();

    if (m_iPitchAndKeylockMode == kOffsetScaleLockOriginalKey) {
        // Normally Pitch slider is not moved in this mode,
        // so we need to correct it manually here
        pitchRateInfo = m_pitchRateInfo.getValue();
        double pitchTweakOctaves = KeyUtils::powerOf2ToOctaveChange(pitchRateInfo.pitchTweakRatio);
        m_pPitch->set(pitchTweakOctaves * 12);
    }

    m_pitchRateInfo.setValue(pitchRateInfo);

    //qDebug() << "KeyControl::slotPitchAndKeylockModeChanged 2" << m_pitchRatio << m_speedSliderPitchRatio;
}

void KeyControl::slotFileKeyChanged(double value) {
    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();
    updateKeyCOs(value, KeyUtils::powerOf2ToOctaveChange(pitchRateInfo.pitchRatio));
}

void KeyControl::updateKeyCOs(double fileKeyNumeric, double pitch) {
    //qDebug() << "updateKeyCOs 1" << pitch;
    mixxx::track::io::key::ChromaticKey fileKey =
            KeyUtils::keyFromNumericValue(fileKeyNumeric);

    QPair<mixxx::track::io::key::ChromaticKey, double> adjusted =
            KeyUtils::scaleKeyOctaves(fileKey, pitch);
    m_pEngineKey->set(KeyUtils::keyToNumericValue(adjusted.first));
    double diff_to_nearest_full_key = adjusted.second;
    m_pEngineKeyDistance->set(diff_to_nearest_full_key);
    //qDebug() << "updateKeyCOs 2" << diff_to_nearest_full_key;
}


void KeyControl::slotSetEngineKey(double key) {
    // Always set to a full key, reset key_distance
    setEngineKey(key, 0.0);
}

void KeyControl::slotSetEngineKeyDistance(double key_distance) {
    setEngineKey(m_pEngineKey->get(), key_distance);
}

void KeyControl::setEngineKey(double key, double key_distance) {
    mixxx::track::io::key::ChromaticKey thisFileKey =
            KeyUtils::keyFromNumericValue(m_pFileKey->get());
    mixxx::track::io::key::ChromaticKey newKey =
            KeyUtils::keyFromNumericValue(key);

    if (thisFileKey == mixxx::track::io::key::INVALID ||
        newKey == mixxx::track::io::key::INVALID) {
        return;
    }

    int stepsToTake = KeyUtils::shortestStepsToKey(thisFileKey, newKey);
    double pitchToTakeOctaves = (stepsToTake + key_distance) / 12.0;

    if (m_iPitchAndKeylockMode == kOffsetScaleLockOriginalKey) {
        double pitchToTakeRatio = KeyUtils::octaveChangeToPowerOf2(pitchToTakeOctaves);
        struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();
        double speedSliderPitchRatio = pitchRateInfo.pitchRatio / pitchRateInfo.pitchTweakRatio;
        double pitchTweakRatio = pitchToTakeRatio / speedSliderPitchRatio;
        pitchToTakeOctaves = KeyUtils::powerOf2ToOctaveChange(pitchTweakRatio);
    }


    m_pPitch->set(pitchToTakeOctaves * 12);
    slotPitchChanged(pitchToTakeOctaves * 12);
    return;
}

void KeyControl::slotPitchChanged(double pitch) {
    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();

    //qDebug() << "KeyControl::slotPitchChanged 1" << pitch <<
    //        pitchRateInfo.pitchRatio <<
    //        pitchRateInfo.pitchTweakRatio <<
    //        pitchRateInfo.tempoRatio;

    double speedSliderPitchRatio = pitchRateInfo.pitchRatio / pitchRateInfo.pitchTweakRatio;

    double pitchRatio = KeyUtils::octaveChangeToPowerOf2(pitch / 12);
    if (m_iPitchAndKeylockMode == kOffsetScaleLockOriginalKey) {
        // Pitch slider presents only the offset, calc absolute pitch
        pitchRatio *= speedSliderPitchRatio;
    }

    pitchRateInfo.pitchRatio = pitchRatio;
    // speedSliderPitchRatio must be unchanged
    pitchRateInfo.pitchTweakRatio =  pitchRateInfo.pitchRatio / speedSliderPitchRatio;

    m_pitchRateInfo.setValue(pitchRateInfo);

    double dFileKey = m_pFileKey->get();
    updateKeyCOs(dFileKey, KeyUtils::powerOf2ToOctaveChange(pitchRatio));

    //qDebug() << "KeyControl::slotPitchChanged 2" << pitch <<
    //        pitchRateInfo.pitchRatio <<
    //        pitchRateInfo.pitchTweakRatio <<
    //        pitchRateInfo.tempoRatio;
}

void KeyControl::slotSyncKey(double v) {
    if (v > 0) {
        EngineBuffer* pOtherEngineBuffer = pickSyncTarget();
        syncKey(pOtherEngineBuffer);
    }
}

void KeyControl::slotResetKey(double v) {
    if (v > 0) {
    	slotSetEngineKey(m_pFileKey->get());
    }
}

bool KeyControl::syncKey(EngineBuffer* pOtherEngineBuffer) {
    if (!pOtherEngineBuffer) {
        return false;
    }

    mixxx::track::io::key::ChromaticKey thisFileKey =
            KeyUtils::keyFromNumericValue(m_pFileKey->get());

    // Get the sync target's effective key, since that is what we aim to match.
    double dKey = ControlObject::get(ConfigKey(pOtherEngineBuffer->getGroup(), "key"));
    mixxx::track::io::key::ChromaticKey otherKey =
            KeyUtils::keyFromNumericValue(dKey);
    double otherDistance = ControlObject::get(ConfigKey(pOtherEngineBuffer->getGroup(), "visual_key_distance"));

    if (thisFileKey == mixxx::track::io::key::INVALID ||
        otherKey == mixxx::track::io::key::INVALID) {
        return false;
    }

    int stepsToTake = KeyUtils::shortestStepsToCompatibleKey(thisFileKey, otherKey);
    double pitchToTakeOctaves = (stepsToTake + otherDistance) / 12.0;

    if (m_iPitchAndKeylockMode == kOffsetScaleLockOriginalKey) {
        double pitchToTakeRatio = KeyUtils::octaveChangeToPowerOf2(pitchToTakeOctaves);
        struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();
        double speedSliderPitchRatio = pitchRateInfo.pitchRatio / pitchRateInfo.pitchTweakRatio;
        double pitchTweakRatio = pitchToTakeRatio / speedSliderPitchRatio;
        pitchToTakeOctaves = KeyUtils::powerOf2ToOctaveChange(pitchTweakRatio);
    }

    m_pPitch->set(pitchToTakeOctaves * 12);
    slotPitchChanged(pitchToTakeOctaves * 12);
    return true;
}

void KeyControl::collectFeatures(GroupFeatureState* pGroupFeatures) const {
    mixxx::track::io::key::ChromaticKey fileKey =
            KeyUtils::keyFromNumericValue(m_pFileKey->get());
    if (fileKey != mixxx::track::io::key::INVALID) {
        pGroupFeatures->has_file_key = true;
        pGroupFeatures->file_key = fileKey;
    }

    mixxx::track::io::key::ChromaticKey key =
            KeyUtils::keyFromNumericValue(m_pEngineKey->get());
    if (key != mixxx::track::io::key::INVALID) {
        pGroupFeatures->has_key = true;
        pGroupFeatures->key = key;
    }
}
