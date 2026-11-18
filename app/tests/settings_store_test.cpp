// SettingsStore and restore_known_calls on files in a temporary folder: the user's own settings are never read or written.
#include "settings_store.hpp"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <limits>

using namespace kz4ap::app;

namespace {

void write_file(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

}  // namespace

TEST(SettingsStore, ChoicesAndLayoutComeBackAsWritten) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const SettingsStore store(dir.filePath(QStringLiteral("settings.ini")));
    UserChoices c;
    c.decoder_only = true;
    c.signed_only = true;
    c.known = {KnownKind::File, QDir(dir.path()).filePath(QStringLiteral("my calls é.scp")).toStdString()};
    c.speed = std::numeric_limits<double>::infinity();
    c.radio_serial = "224002AB12";
    c.center_khz = 14031.5;
    c.sample_rate_hz = 768000;
    c.if_gain_reduction_db = 33;
    c.lna_state = 4;
    c.auto_floor = false;
    c.floor_dbfs = -101.0;
    c.range_db = 64.0;
    c.recording_folder = dir.path().toStdString();
    store.write_choices(c);
    store.write_layout(QStringLiteral("geometry"), QByteArray("\x01\x02\x00\xff", 4));
    const SettingsStore again(store.path());
    EXPECT_EQ(again.read_choices(), c);
    EXPECT_EQ(again.read_layout(QStringLiteral("geometry")), QByteArray("\x01\x02\x00\xff", 4));
    EXPECT_TRUE(again.read_layout(QStringLiteral("nothing")).isEmpty());
    // Downloaded carries no path, and a later write of None drops the file's.
    c.known = {KnownKind::Downloaded, {}};
    store.write_choices(c);
    EXPECT_EQ(again.read_choices().known, c.known);
    EXPECT_FALSE(QSettings(store.path(), QSettings::IniFormat).contains(QStringLiteral("calls/known_file")));
}

TEST(SettingsStore, NoPathReadsTheDefaultsAndWritesNothing) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const SettingsStore none;
    EXPECT_FALSE(none.enabled());
    UserChoices c;
    c.decoder_only = true;
    none.write_choices(c);
    none.write_layout(QStringLiteral("geometry"), QByteArray("x"));
    EXPECT_EQ(none.read_choices(), UserChoices{});
    EXPECT_TRUE(none.read_layout(QStringLiteral("geometry")).isEmpty());
    EXPECT_TRUE(QDir(dir.path()).isEmpty());
}

TEST(SettingsStore, AFileEditedBadlyGivesTheDefaults) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    write_file(path, "[calls]\nmode=sideways\nknown=file\n[transport]\nspeed=3\n[radio]\nrate_hz=250000\nif_gain_reduction_db=x\n"
                     "[waterfall]\nauto_floor=maybe\nrange_db=500\n");
    const UserChoices c = SettingsStore(path).read_choices();
    EXPECT_EQ(c, UserChoices{});
}

TEST(SettingsStore, ASavedListThatIsGoneGivesNoListAndSaysSo) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString gone = dir.filePath(QStringLiteral("gone.scp"));
    const RestoredKnown r = restore_known_calls({KnownKind::File, gone.toStdString()}, dir.path());
    EXPECT_EQ(r.list, nullptr);
    EXPECT_TRUE(r.problem);
    EXPECT_TRUE(r.status.contains(QStringLiteral("no longer exists"))) << r.status.toStdString();
    EXPECT_TRUE(r.status.contains(QDir::toNativeSeparators(gone))) << r.status.toStdString();
    // A file that is refused (no call in it): no list either, and no throw.
    const QString empty = dir.filePath(QStringLiteral("empty.scp"));
    write_file(empty, "!!Order,1,1\n");
    const RestoredKnown refused = restore_known_calls({KnownKind::File, empty.toStdString()}, dir.path());
    EXPECT_EQ(refused.list, nullptr);
    EXPECT_TRUE(refused.problem);
}

TEST(SettingsStore, ASavedFileOrDownloadedCopyIsReadFromDisk) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString mine = dir.filePath(QStringLiteral("mine.scp"));
    write_file(mine, "K1ABC\nW1AW\n");
    const RestoredKnown own = restore_known_calls({KnownKind::File, mine.toStdString()}, dir.path());
    ASSERT_NE(own.list, nullptr);
    EXPECT_EQ(own.list->size(), 2u);
    EXPECT_FALSE(own.problem);
    EXPECT_EQ(QFileInfo(own.path), QFileInfo(mine));
    // Downloaded: the copy the button keeps, MASTER.SCP in the kept folder; with none there, no list and a status.
    const RestoredKnown missing = restore_known_calls({KnownKind::Downloaded, {}}, dir.path());
    EXPECT_EQ(missing.list, nullptr);
    EXPECT_TRUE(missing.problem);
    write_file(dir.filePath(QStringLiteral("MASTER.SCP")), "!!Order,1,1\nDL1ABC\nJA1XYZ\nVK2DEF\n");
    const RestoredKnown kept = restore_known_calls({KnownKind::Downloaded, {}}, dir.path());
    ASSERT_NE(kept.list, nullptr);
    EXPECT_EQ(kept.list->size(), 3u);
    EXPECT_TRUE(kept.status.contains(QStringLiteral("downloaded"))) << kept.status.toStdString();
    // None: nothing read, nothing said.
    const RestoredKnown none = restore_known_calls({}, dir.path());
    EXPECT_EQ(none.list, nullptr);
    EXPECT_FALSE(none.problem);
    EXPECT_TRUE(none.status.isEmpty());
}
