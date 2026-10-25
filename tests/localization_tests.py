"""Check English coverage, gameplay data preservation and font coverage."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from localization import CJK, TOKENS, decode_literal, inventory, load_catalog, translate_cpp
from prepare_locale import prepare


class LocalizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stage = prepare()

    def test_complete_translation(self):
        self.assertFalse(inventory(self.stage))
        catalog = load_catalog()
        self.assertTrue(all(value in catalog['strings'] for value in inventory()))

    def test_numeric_game_data_unchanged(self):
        def without_text(path):
            return TOKENS.sub(lambda m: '""' if m[0].startswith('"') else m[0],
                              path.read_text(encoding='utf-8'))
        self.assertEqual(without_text(ROOT / 'generated/game_data.cpp'),
                         without_text(self.stage / 'generated/game_data.cpp'))

    def test_cpp_comments_and_character_literals(self):
        source = "// 中文\nconst char quote='\"'; const char* name=\"中文\"; /* 中文 */"
        result, missing = translate_cpp(source, {'中文': 'English'})
        self.assertFalse(missing)
        self.assertEqual(result, source.replace('name="中文"', 'name="English"'))

    def test_ascii_font_coverage(self):
        import re
        source = (self.stage / 'generated/font_data.cpp').read_text(encoding='utf-8')
        self.assertEqual({int(cp) for cp in re.findall(r'^\{(\d+),', source, re.M)}, set(range(32,127)))

    def test_ui_literals_have_available_glyphs(self):
        # Translation coverage alone missed the non-CJK route arrow, which
        # rendered as a fallback box in the English ASCII-only font.
        for name in ('main.cpp', 'render_ui.cpp', 'render.bn_iwram.cpp'):
            source = (self.stage / 'src' / name).read_text(encoding='utf-8')
            for token in TOKENS.finditer(source):
                if token[0].startswith('"'):
                    value = decode_literal(token[0])
                    self.assertFalse(any(ord(ch) > 126 for ch in value),
                                     f'{name}: unavailable glyph in {value!r}')

    def test_english_message_wrapping(self):
        import shutil
        import subprocess
        import tempfile
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('Host g++ is required for the generated renderer check')
        source = (self.stage / 'src/render_ui.cpp').read_text(encoding='utf-8')
        function = source[source.index('void Render::ui_message('):source.index('// Raised/recessed')]
        # The generated English UI glyphs all advance seven pixels. Compile
        # the actual generated function with a recording drawing surface.
        harness = r'''
#include <cstdint>
#include <cassert>
#include <string>
#include <vector>
int advance(uint32_t,bool){return 7;}
struct Render {
    std::vector<std::string> lines;
    void ui_text(int,int,const char* s,int,int){lines.emplace_back(s);}
    void ui_message(int,int,int,const char*,int);
};
'''
        cases = r'''
int main(){Render r;
auto check=[&](const char* s,int width,std::vector<std::string> expected){
    r.lines.clear();r.ui_message(0,0,width,s,0);assert(r.lines==expected);
};
check(nullptr,69,{});check("",69,{});check("   ",69,{});
check("Alpha beta",69,{"Alpha beta"});
check("Alpha beta gamma",69,{"Alpha beta","gamma"});
check("Alpha beta gamma",41,{"Alpha","beta gamma"});
check("   Alpha beta",100,{"Alpha beta"});
check("Alpha   beta",41,{"Alpha","beta"});
check("Alpha ",34,{"Alpha"});
check("ABCDEFGHIJK",34,{"ABCDE","FGHIJK"});
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'message.cpp').write_text(harness + function + cases, encoding='utf-8')
            subprocess.run([compiler, '-std=c++17', '-O2', str(path / 'message.cpp'),
                            '-o', str(path / 'message')], check=True, capture_output=True)
            subprocess.run([str(path / 'message')], check=True, capture_output=True)

    def test_reviewed_english_layout_fixes(self):
        battle = (self.stage / 'src/render_ui.cpp').read_text(encoding='utf-8')
        combat = (self.stage / 'src/render.bn_iwram.cpp').read_text(encoding='utf-8')
        main = (self.stage / 'src/main.cpp').read_text(encoding='utf-8')
        self.assertIn('number(x+15,24,fighter.hp', combat)
        self.assertIn('number(x+59,24,fighter.mp', combat)
        self.assertIn('text(x+44,24,"MP"', battle)
        self.assertIn('text(x+80,24,"#"', battle)
        helper = (self.stage / 'include/english_ui.h').read_text(encoding='utf-8')
        for collision in ("p[2]=='i'?'I':'B'", "p[2]=='n'?'N':'O'", "p[2]=='a'?'S':'U'"):
            self.assertIn(collision, helper)
        for phrase in ('"Finish"', '"Time for administration."', '"A/Start:Next"', '"By"'):
            self.assertIn(phrase, main)

    def test_second_round_layout_fixes(self):
        main = (self.stage / 'src/main.cpp').read_text(encoding='utf-8')
        map_render = (self.stage / 'src/render.bn_iwram.cpp').read_text(encoding='utf-8')
        helper = (self.stage / 'include/english_ui.h').read_text(encoding='utf-8')
        self.assertIn('c.king==game.campaign.player ? "Own"', map_render)
        self.assertIn('side==0 ? "Atk" : "Def"', main)
        self.assertIn('army.garrisoned ? "Camp" : army.retreating ? "Retreat" : "March"', main)
        self.assertIn('english_compact_general(commander)', main)
        self.assertIn('english_compact_city(army.origin)', main)
        self.assertIn('english_compact_force(o.force)', main)
        self.assertIn('english_compact_formation(officer.formation)', main)
        self.assertIn('english_compact_king(', main)
        self.assertIn('english_compact_general(int id)', helper)

    def test_second_round_aliases_fit_their_columns(self):
        import re
        font = (self.stage / 'generated/font_data.cpp').read_text(encoding='utf-8')
        glyphs = {int(cp): int(width) for cp, width in re.findall(r'^\{(\d+),\{[^}]+\},(\d+)\}', font, re.M)}
        def width(text):
            return max(0, sum(glyphs[ord(ch)] + 1 for ch in text) - 1)
        for alias in ('Camp','March','Atk','Def','F','R',
                      'Sword','Spear','Bow','Glaive','Knife','Flail','Rebel','Tribal','Rattan','Fight','Hammer'):
            self.assertLessEqual(width(alias), 44, alias)
        self.assertLessEqual(width('Retreat'),50)
        self.assertLessEqual(width('March'),50)
        for alias in ('Square','Circle','Wedge','Geese','Mystic','Fish','Hook'):
            self.assertLessEqual(width(alias),43,alias)
        for alias in ('Xiangp.','Xiangy.','Shangy.','Jiangl.','Jiangz.','Pingy.','Hongn.',"Chang'n",'Tiansh.','Shouch.','Hanzh.','Jiann.','Changs.','Jiangx.','Lingl.'):
            self.assertLessEqual(width(alias),53,alias)
        for alias in ('Sun J.','D. Zhuo','D. Yuan','G. Zan','Z. Jiao','Y. Shao','Y. Shu','L. Biao','Y. Bai',
                      'W. Lang','L.Zhang','G. Jing','Q. Xuan','T. Qian','K. Rong','K. Xiu','Q. Mao','J. Xuan','M. Huo','Sun Q.','Z. Lu'):
            self.assertLessEqual(width(alias), 48, alias)

    def test_compact_rank_titles_fit_profile_fields(self):
        import re
        roles = load_catalog()['short_roles']
        data = (self.stage / 'generated/game_data.cpp').read_text(encoding='utf-8')
        block = data.split('const SkillDef skill_defs[skill_count] = {', 1)[1].split('};', 1)[0]
        titles = re.findall(r'^\{"[^"\\]*","([^"\\]*)",', block, re.M)
        self.assertEqual(len(titles), 50)
        for title in titles:
            shown = roles.get(title, title)
            self.assertTrue(shown.isascii(), shown)
            self.assertLessEqual(len(shown) * 7 - 1, 54, title)

    def test_third_round_screen_fixes(self):
        main = (self.stage / 'src/main.cpp').read_text(encoding='utf-8')
        helper = (self.stage / 'include/english_ui.h').read_text(encoding='utf-8')
        for expected in ('"Lv",sg::muted_ink); render.ui_number(80,48',
                         '"LOY",sg::muted_ink); render.ui_number(153,48',
                         '"Trps",sg::muted_ink); render.ui_number(158,69',
                         'render.panel("Items")',
                         '"At" : "Army",sg::muted_ink,36',
                         'render.ui_text(150,y,sg::city_defs[location].name',
                         '"Other side"',
                         '"Sel",sg::muted_ink); render.ui_number(175,28',
                         '"Chosen" : game.campaign.officers[g].hp ? "Ready" : "Resting"',
                         '"#",sg::muted_ink);'):
            self.assertIn(expected, main)
        for expected in ('english_compact_equipped_item(int item,const char* name)',
                         'item==41 ? "Flying Light"'):
            self.assertIn(expected, helper)
        for phrase in ('"Unlocks troop type"','"Unlocks formation"',
                       '"Faster attacks while moving"','"Special gear effect"'):
            self.assertIn(phrase,main)

    def test_fourth_round_screen_fixes(self):
        main = (self.stage / 'src/main.cpp').read_text(encoding='utf-8')
        helper = (self.stage / 'include/english_ui.h').read_text(encoding='utf-8')
        for expected in ('"Where",sg::muted_ink,80',
                         'outcomes[]={"In City","March","Retreat","Captive","Missing","Rescued"}',
                         'render.row(y,sg::english_compact_general(entry.general)',
                         'sg::english_compact_city(entry.city)',
                         'render.ui_text(181,47,"Lv",sg::muted_ink,24)',
                         'render.ui_text(153,63,"Trps",sg::muted_ink,34)',
                         'render.ui_text(10,116,"Skills",sg::muted_ink,52)',
                         'sg::english_compact_skill(skill),sg::text_ink,72',
                         'render.heading(51,29,sg::english_compact_general(g),140)',
                         'render.ui_text(9,70,"Troops",sg::muted_ink,48)',
                         'sg::english_compact_force(i)',
                         'known ? "Yes" : "No"'):
            self.assertIn(expected, main)
        self.assertIn('inline const char* english_compact_skill(const SkillDef* skill)', helper)

    def test_fifth_round_screen_fixes(self):
        main = (self.stage / 'src/main.cpp').read_text(encoding='utf-8')
        helper = (self.stage / 'include/english_ui.h').read_text(encoding='utf-8')
        for expected in ('english_compact_general(winner),67',
                         'english_compact_general(loser),58',
                         'battle.round_result_kind==2 ? "Captive"',
                         'before && after>before ? "Lv+" : "Lv"',
                         '"Officer Details","Officer Position","Withdraw"',
                         'render.ui_text(121,28,"Complete"',
                         'english_compact_general(general),sg::muted_ink,78',
                         'render.ui_center(14,88,212,sg::king_defs[game.campaign.scenario][game.campaign.player].name',
                         'english_compact_force(officer.force),sg::text_ink,55',
                         'english_compact_formation(officer.formation),sg::muted_ink,54',
                         'english_compact_role(job->title)',
                         'english_compact_role(skill->title)',
                         'english_compact_skill(skill),sg::text_ink,105'):
            self.assertIn(expected, main)
        self.assertIn('inline const char* english_compact_role(const char* name)', helper)


if __name__ == '__main__':
    unittest.main()
