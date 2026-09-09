#include <unity.h>

#include <cstring>

#include "rom_index.h"

using namespace tabulous::rom_index;

void setUp() {}
void tearDown() {}

void test_groups_by_first_letter() {
  TEST_ASSERT_EQUAL(2, groupOf("Alter Ego"));
  TEST_ASSERT_EQUAL(2 + ('S' - 'A'), groupOf("super mario"));
  TEST_ASSERT_EQUAL(kOther, groupOf("1942 (USA)"));
  TEST_ASSERT_EQUAL(kOther, groupOf("'89 Dennou"));  // digit before any letter
  TEST_ASSERT_EQUAL(2 + ('Z' - 'A'), groupOf("\"Zelda\""));
  TEST_ASSERT_EQUAL(kOther, groupOf("---"));
  TEST_ASSERT_EQUAL_STRING("*", groupLabel(kFavourites));
  TEST_ASSERT_EQUAL_STRING("#", groupLabel(kOther));
  TEST_ASSERT_EQUAL_STRING("Z", groupLabel(kGroups - 1));
}

void test_sorted_into_contiguous_groups() {
  Index ix(".nes");
  ix.add("/roms", "Zelda.nes", kFlash);
  ix.add("/roms/S", "Super Mario Bros. (World).nes", kCard);
  ix.add("/roms", "1942.nes", kCard);
  ix.add("/roms", "alter_ego.nes", kFlash);
  ix.add("/roms", "Solomon's Key.nes", kCard);
  ix.add("/roms", "notes.txt", kCard);   // wrong extension: ignored
  ix.add("/roms", "Tetris.gb", kCard);   // another system's: ignored
  ix.finish();

  TEST_ASSERT_EQUAL(5, ix.size());
  TEST_ASSERT_EQUAL_STRING("1942", ix.at(0).name);
  TEST_ASSERT_EQUAL_STRING("alter ego", ix.at(1).name);
  TEST_ASSERT_EQUAL_STRING("Solomon's Key", ix.at(2).name);
  TEST_ASSERT_EQUAL_STRING("Super Mario Bros. (World)", ix.at(3).name);
  TEST_ASSERT_EQUAL_STRING("Zelda", ix.at(4).name);
  TEST_ASSERT_EQUAL_STRING("/roms/S/Super Mario Bros. (World).nes", ix.at(3).path);
  TEST_ASSERT_EQUAL_STRING("Super Mario Bros. (World).nes", ix.at(3).file);

  const int s = 2 + ('S' - 'A');
  TEST_ASSERT_EQUAL(2, ix.groupBegin(s));
  TEST_ASSERT_EQUAL(2, ix.groupCount(s));
  TEST_ASSERT_EQUAL(1, ix.groupCount(kOther));
  TEST_ASSERT_EQUAL(0, ix.groupCount(2 + ('B' - 'A')));
  TEST_ASSERT_EQUAL(4, ix.groupBegin(kGroups - 1));
}

void test_favourites_round_trip() {
  Index ix(".nes");
  ix.add("/roms", "Zelda.nes", kFlash);
  ix.add("/roms", "Metroid.nes", kCard);
  ix.add("/roms", "Contra.nes", kCard);
  ix.finish();

  ix.applyFavourites("Zelda.nes\r\nNot Here.nes\n\nContra.nes\n");
  TEST_ASSERT_EQUAL(2, ix.favouriteCount());
  TEST_ASSERT_EQUAL_STRING("Contra", ix.at(ix.favouriteAt(0)).name);
  TEST_ASSERT_EQUAL_STRING("Zelda", ix.at(ix.favouriteAt(1)).name);
  // The one that matched nothing is kept, not silently dropped.
  TEST_ASSERT_EQUAL_STRING("Contra.nes\nZelda.nes\nNot Here.nes\n",
                           ix.favouritesText().c_str());

  ix.setFavourite(ix.favouriteAt(1), false);  // un-star Zelda
  TEST_ASSERT_EQUAL(1, ix.favouriteCount());
  ix.setFavourite(1, true);  // Metroid
  TEST_ASSERT_EQUAL(2, ix.favouriteCount());
  TEST_ASSERT_EQUAL_STRING("Contra.nes\nMetroid.nes\nNot Here.nes\n",
                           ix.favouritesText().c_str());
}

void test_favourites_survive_rescan() {
  Index ix(".nes");
  ix.applyFavourites("Metroid.nes\n");
  ix.add("/roms", "Metroid.nes", kCard);
  ix.finish();
  TEST_ASSERT_EQUAL(1, ix.favouriteCount());
  TEST_ASSERT_TRUE(ix.at(0).fav);

  ix.clear();
  ix.add("/roms", "Contra.nes", kCard);
  ix.finish();
  TEST_ASSERT_EQUAL(0, ix.favouriteCount());
  TEST_ASSERT_EQUAL_STRING("Metroid.nes\n", ix.favouritesText().c_str());
}

void test_same_title_on_flash_and_card_is_one_favourite() {
  Index ix(".nes");
  ix.add("/roms", "Chase.nes", kFlash);
  ix.add("/roms/C", "Chase.nes", kCard);
  ix.finish();
  TEST_ASSERT_EQUAL(kFlash, ix.at(0).source);
  ix.setFavourite(1, true);
  TEST_ASSERT_TRUE(ix.at(0).fav);
  TEST_ASSERT_EQUAL(2, ix.favouriteCount());
  TEST_ASSERT_EQUAL_STRING("Chase.nes\nChase.nes\n", ix.favouritesText().c_str());
}

void test_extension_decides_what_belongs() {
  Index gb(".gb");
  gb.add("/gb", "Tetris (World).gb", kCard);
  gb.add("/gb", "Metroid.nes", kCard);
  gb.add("/gb", ".gb", kCard);  // extension with no name in front of it
  gb.finish();
  TEST_ASSERT_EQUAL(1, gb.size());
  TEST_ASSERT_EQUAL_STRING("Tetris (World)", gb.at(0).name);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_extension_decides_what_belongs);
  RUN_TEST(test_groups_by_first_letter);
  RUN_TEST(test_sorted_into_contiguous_groups);
  RUN_TEST(test_favourites_round_trip);
  RUN_TEST(test_favourites_survive_rescan);
  RUN_TEST(test_same_title_on_flash_and_card_is_one_favourite);
  return UNITY_END();
}
