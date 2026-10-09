// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace QuestCampaign
{
inline bool FlatSequence(bool campaign, bool scriptedCamera, bool letterbox,
    bool inputDisabled, bool movie)
{
    return campaign && (scriptedCamera || letterbox || inputDisabled || movie);
}

// A blocking upstream movie temporarily owns XR submission, never engine updates.
// Driver can also be a host fixture exercising real ownership transitions.
template<class Driver> class MovieFrameLease
{
public:
    explicit MovieFrameLease(Driver &driver) : driver(driver), restore(driver.active())
    {
        if (restore) driver.end();
    }
    ~MovieFrameLease()
    {
        if (driver.active()) driver.end();
        if (restore) driver.begin();
    }
    bool poll() { return driver.active() || driver.begin(); }
    void present() { if (driver.active()) driver.end(); }
    MovieFrameLease(const MovieFrameLease &) = delete;
    MovieFrameLease &operator=(const MovieFrameLease &) = delete;
private:
    Driver &driver;
    bool restore;
};
}
