// Verification for the single-instance guard.
//
// Why this needs a probe rather than a look: both failure modes are silent. If acquire()
// answers "taken" when nothing holds the name, the application refuses to start and the
// user gets a message box about a QShot that is not running -- unstartable, with nothing
// in any log. If it answers "free" while something does hold it, two copies run and the
// hotkey conflict the guard exists to prevent is back.
//
// The named mutex makes both testable inside one process. The object belongs to the logon
// session, not to a process, so a second guard here sees exactly the ERROR_ALREADY_EXISTS
// a second QShot would -- no second process, no window, no flashing.
//
// The name is injectable for that reason too: this probe takes a name of its own, so a
// QShot the user happens to have open can neither fail this probe nor be stopped by it.
//
// Run: probe_single_instance.exe      (no widgets, no windows, nothing flashes)

#include <QCoreApplication>
#include <QString>
#include <cstdio>
#include <memory>

#include "core/ISingleInstance.h"
#include "core/PlatformFactory.h"
#include "platform/windows/WinSingleInstance.h"

using namespace qshot;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

int main(int, char**) {
    printf("[1] the factory produces a guard on this platform\n");
    {
        std::unique_ptr<ISingleInstance> viaFactory = PlatformFactory::createSingleInstance();
        check(viaFactory != nullptr,
              "an implementation exists, so a second QShot is refused");

        // Not asserted: this is the one name the probe does not own, so a "held" answer
        // means the user has QShot open, which is not a defect. Printed because it is the
        // only reading taken against a genuine second instance rather than against
        // ourselves, and it costs one line to see.
        if (viaFactory) {
            printf("       (informational) the application's own name is %s\n",
                   viaFactory->acquire() ? "free" : "held by a running QShot");
        }
    }

    // A name of this run's own: the process id makes it unique, so the probe can neither
    // be fooled by a running QShot nor disturb one.
    const QString name =
        QStringLiteral("Local\\QShot.Probe.%1").arg(QCoreApplication::applicationPid());

    printf("\n[2] the second holder is refused\n");
    {
        WinSingleInstance first(name);
        check(first.acquire(), "the first guard takes the name");

        WinSingleInstance second(name);
        check(!second.acquire(), "a second guard with the same name is refused");

        // The interface promises this, and the application relies on it: registerGlobalHotkeys()
        // and the retry path can both land here.
        check(first.acquire(), "asking the holder again still says yes");
    }

    printf("\n[3] the name is free again once the holder is gone\n");
    {
        WinSingleInstance third(name);
        check(third.acquire(), "a new guard takes the name the previous one released");
    }

    printf("\n[4] the name selects the object, so this is not process-global state\n");
    {
        WinSingleInstance a(name);
        WinSingleInstance b(name + QStringLiteral(".other"));
        check(a.acquire(), "the first name is taken");
        // Without this, [2] would pass just as well if acquire() were answering from a
        // static flag and the mutex never entered the picture.
        check(b.acquire(), "a different name is independent of it");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
