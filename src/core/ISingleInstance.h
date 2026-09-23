#pragma once

namespace qshot {

/**
 * A guard that only one process may hold at a time.
 *
 * Two copies of QShot cannot coexist: they want the same global hotkey, they both
 * put an icon in the tray, and they both own the same history directory. The second
 * one is not "another window", it is a broken copy -- it cannot register the hotkey,
 * so it sits in the tray claiming the shortcut is taken while looking exactly like
 * the working instance.
 *
 * Windows implements this with a named kernel mutex, macOS and Linux with a named
 * semaphore or an flock'd file. The interface only exposes what main() needs, which
 * is a yes/no answer.
 *
 * The guard is released by the destructor, so it has to outlive the application
 * object. That is also why the implementation must be an OS-owned object rather than
 * a lock file: the OS releases a mutex when the process dies, however it dies, while
 * a lock file left behind by a crash would lock the user out of their own
 * application until they found and deleted it.
 */
class ISingleInstance {
public:
    virtual ~ISingleInstance() = default;

    /// Take the guard. False means another instance already holds it.
    ///
    /// Calling this twice on the same object succeeds, so a caller may use it as a
    /// plain "am I the only one?" question.
    virtual bool acquire() = 0;
};

} // namespace qshot
