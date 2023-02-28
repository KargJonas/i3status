// vim:ts=4:sw=4:expandtab
#include <stdio.h>
#include <stdlib.h>
#include <yajl/yajl_gen.h>
#include <time.h>
#include "i3status.h"

#define MAX_STRING_SIZE     (128)
#define BINARY_BASE         (1024UL)
#define BLIP_LIST_SIZE      (121)
#define QUOTE_LIST_SIZE     (18)
#define EMOJI_LIST_SIZE     (346)

bool breathing_step = 0;

const char *blip_list[] = { "(^_^)", "[o_o]", "(^.^)", "(\".\")", "($.$)", "{-_-}", "(•_•)", "( •_•)", ">⌐■-■", "(⌐■_■)", "─=≡Σ((( つ◕ل͜◕)つ", "(╯°□°)╯︵ ┻━┻ ︵ ╯(°□° ╯)", "(ღ˘⌣˘ღ)", "୧༼ಠ益ಠ╭∩╮༽", "┌П┐(ಠ_ಠ)", "ಠ_ರೃ", "= _ =", "༼ つ ╹ ╹ ༽つ", "(´・ω・)っ由", "༼ つ ◕_◕ ༽つ", "(•-•)⌐", "(*・‿・)ノ⌒*:･ﾟ✧", "(⌐ ͡■ ͜ʖ ͡■)", "( ͡° ͜ʖ ͡°)ﾉ⌐■-■", "(ﾉ☉ヮ⚆)ﾉ ⌒*:･ﾟ✧", "(_゜_゜_)", "(з๏ε)", "(☞ﾟ∀ﾟ)☞", "( ´◔ ω◔`) ノシ", "︻╦╤─", "༼つಠ益ಠ༽つ ─=≡ΣO))", "٩(^‿^)۶", "٩( ๑╹ ꇴ╹)۶", "ᕕ( ᐛ )ᕗ", "♥", "(ʘ‿ʘ)╯", "\\(°Ω°)/", "._.)/\\(._.", "( ｀皿´)｡ﾐ/", "(づ｡◕‿‿◕｡)づ", "┐｜･ิω･ิ#｜┌", "୧(▲ᴗ▲)ノ", "(っ´ω`c)♡", "ଘ(੭*ˊᵕˋ)੭* ̀ˋ ɪɴᴛᴇʀɴᴇᴛ", "(❍ᴥ❍ʋ)", "(¬,‿,¬)", "≧◡≦", "┬┴┬┴┤Ɵ͆ل͜Ɵ͆ ༽ﾉ", "~\\(≧▽≦)/~", "(づ ￣ ³￣)づ", "／人◕ ‿‿ ◕人＼", "(̿▀̿‿ ̿▀̿ ̿)", "_(:3」∠)_", "( ͡° ͜ʖ ͡°)", "[̲̅$̲̅(̲̅ ͡° ͜ʖ ͡°̲̅)̲̅$̲̅]", "(ง ͠° ͟ʖ ͡°)ง", "(ノ ͡° ͜ʖ ͡°ノ) ︵ ( ͜。 ͡ʖ ͜。)", "( ͡°( ͡° ͜ʖ( ͡° ͜ʖ ͡°)ʖ ͡°) ͡°)", "¯\\_( ͡° ͜ʖ ͡°)_/¯", "( ಠ ͜ʖ ರೃ)", "┬┴┬┴┤( ͡° ͜ʖ├┬┴┬┴", "ᕦ( ͡° ͜ʖ ͡°)ᕤ", "╰( ͡° ͜ʖ ͡° )つ──☆*:・ﾟ", "███▒▒▒▒▒▒▒", "L(° O °L)", "(ಡ_ಡ)☞", "ᕦ(⩾﹏⩽)ᕥ", "♥‿♥", "ʕ♥ᴥ♥ʔ", "꒰ ꒡⌓꒡꒱", "-`ღ´-", "ヽ(｀Д´)⊃━☆ﾟ. * ･ ｡ﾟ,", "(/¯◡ ‿ ◡)/¯ ~ ┻━┻", "\\(°^°)/", "ಠ_ಠ", "\\m/,(> . <)_\\m/", "ಡ_ಡ", "༼ ༎ຶ ෴ ༎ຶ༽", "☜(꒡⌓꒡)", "┌(◉ ͜ʖ◉)つ┣▇▇▇═──", "(⌐⊙_⊙)", "( ͡° ͜ °)", "→_←", "／人◕ __ ◕人＼", "(っˆڡˆς)", "\t＼(＾O＾)／", "ლ(`◉◞౪◟◉‵ლ)", "~=[,_,]:3", "@^@", "( º﹃º )", "\t(⊙＿⊙')", "◕_◕", "┬──┬ ノ(ò_óノ)", "(눈_눈)", "(◞థ౪థ)ᴖ", "✌(-‿-)✌", "(__>-", "⊙ω⊙", "( •_•)O*¯`·.¸.·´¯`°Q(•_• )", "._.", "(˶‾᷄ ⁻̫ ‾᷅˵)", "(☞ﾟヮﾟ)☞", "ʕ •́؈•̀)", "(•ᴥ• )́`́'́`́'́⻍", "\\( ﾟヮﾟ)/", "(☝ ՞ਊ ՞)☝", "O=('-'Q)", "t(ಠ益ಠt)", "(ノಠ益ಠ)ノ彡┻━┻", "(=^･ｪ･^=))ﾉ彡☆", "ò_ô", "(•̀ᴗ•́)و ̑̑", "୧༼ಠ益ಠ༽୨", "⊹╰(⌣ʟ⌣)╯⊹", "(◔_◔)", "✿ڿڰۣ—", "(╯°□°)╯", "ε(´סּ︵סּ`)з", "ヽ༼ຈʖ̯ຈ༽ﾉ", "( ͡° ʖ̯ ͡°)", "[¬º-°]¬", "╚(•⌂•)╝" };
const char *breathing_instructions_list[] = { "Breathe in ", "Breathe out" };
const char *quote_list[] = { "What is love?", "We've been trying to reach you about your car's extended warranty", "π ≈ 3", "Roses are red, violets are blue, ChatGPT codes better than you", "How late is it?", "Stop looking at me", "Error on line 69", "Go outside!", "404 message not found", "Ignore the next message", "Today is a bad day for lottery tickets", "Remember the Cant!", "Never trust a fortune teller", "Always trust a fortune teller", "13 is my lucky number", "run.", "You can read??", "God is dead and we killed him.", "... where was I?" };
const char *emoji_list[] = { "😀", "😃", "😄", "😁", "😆", "😅", "🤣", "😂", "🙂", "🙃", "🫠", "😉", "😊", "😇", "🥰", "😍", "🤩", "😘", "😗", "😚", "😙", "🥲", "😋", "😛", "😜", "🤪", "😝", "🤑", "🤗", "🤭", "🫢", "🫣", "🤫", "🤔", "🫡", "🤐", "🤨", "😐", "😑", "😶", "🫥", "😶", "😏", "😒", "🙄", "😬", "🫨", "😮", "🤥", "😌", "😔", "😪", "🤤", "😴", "😷", "🤒", "🤕", "🤢", "🤮", "🤧", "🥵", "🥶", "🥴", "😵", "😵", "🤯", "🤠", "🥳", "🥸", "😎", "🤓", "🧐", "😕", "🫤", "😟", "🙁", "😮", "😯", "😲", "😳", "🥺", "🥹", "😦", "😧", "😨", "😰", "😥", "😢", "😭", "😱", "😖", "😣", "😞", "😓", "😩", "😫", "🥱", "😤", "😡", "😠", "🤬", "😈", "👿", "💀", "💩", "🤡", "👹", "👺", "👻", "👽", "👾", "🤖", "😺", "😸", "😹", "😻", "😼", "😽", "🙀", "😿", "😾", "💋", "👋", "🤚", "🖐", "✋", "🖖", "🫱", "🫲", "🫳", "🫴", "👌", "🤌", "🤏", "🤞", "🫰", "🤟", "🤘", "🤙", "👈", "👉", "👆", "🖕", "👇", "🫵", "👍", "👎", "✊", "👊", "🤛", "🤜", "👏", "🙌", "🫶", "👐", "🤲", "🤝", "🙏", "💅", "🤳", "💪", "🦾", "🦿", "🦵", "🦶", "👂", "🦻", "👃", "🧠", "🫀", "🫁", "🦷", "🦴", "👀", "👅", "👄", "🫦", "👶", "🧒", "👦", "👧", "🧑", "👱", "👨", "🧔", "👨", "👨", "👨", "👨", "👩", "👩", "🧑", "👩", "🧑", "👩", "🧑", "👩", "🧑", "👱", "👱", "🧓", "👴", "👵", "🙍", "🙍", "🙍", "🙎", "🙎", "🙎", "🙅", "🙅", "🙅", "🙆", "🙆", "🙆", "💁", "💁", "💁", "🙋", "🙋", "🙋", "🧏", "🧏", "🧏", "🙇", "🙇", "🙇", "🤦", "🤦", "🤦", "🤷", "🤷", "🤷", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "🧑", "👨", "👩", "👮", "👮", "👮", "🕵", "🕵", "🕵", "💂", "💂", "💂", "🥷", "👷", "👷", "👷", "🫅", "🤴", "👸", "👳", "👳", "👳", "👲", "🧕", "🤵", "🤵", "🤵", "👰", "👰", "👰", "🤰", "🫃", "🫄", "🤱", "👩", "👨", "🧑", "👼", "🎅", "🤶", "🧑", "🦸", "🦸", "🦸", "🦹", "🦹", "🦹", "🧙", "🧙", "🧙", "🧚", "🧚", "🧚", "🧛", "🧛", "🧛", "🧜", "🧜", "🧜", "🧝", "🧝", "🧝", "🧞", "🧞", "🧞", "🧟", "🧟", "🧟", "🧌" };

void print_blipper(blipper_ctx_t *ctx) {
    char *outwalk = ctx->buf;

#if defined(__linux__)
    const char *format = ctx->format;
    const char *output_color = NULL; // can be set to: color_bad, color_degraded

    if (output_color) {
        START_COLOR(output_color);
    }

    srand(time(NULL));
    const char *blip = blip_list[rand() % BLIP_LIST_SIZE];
    const char *breathing_instruction = breathing_instructions_list[breathing_step = !breathing_step];
    const char *quote = quote_list[rand() % QUOTE_LIST_SIZE];
    char emoji[MAX_STRING_SIZE] = "";

    for (int i = 0; i < ctx->n_emojis; i++) {
        strcat(emoji, emoji_list[rand() % EMOJI_LIST_SIZE]);
    }

    char blip_string[MAX_STRING_SIZE];
    char breathing_instruction_string[MAX_STRING_SIZE];
    char quote_string[MAX_STRING_SIZE];
    char emoji_string[MAX_STRING_SIZE];

    sprintf(blip_string, "%s", blip);
    sprintf(breathing_instruction_string, "%s", breathing_instruction);
    sprintf(quote_string, "%s", quote);
    sprintf(emoji_string, "%s", emoji);

    // Introduce placeholders for config file here
    placeholder_t placeholders[] = {
        {.name = "%blip", .value = blip_string},
        {.name = "%breathe", .value = breathing_instruction_string},
        {.name = "%quote", .value = quote_string},
        {.name = "%emoji", .value = emoji_string}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(format, &placeholders[0], (int)num);

    OUTPUT_FORMATTED;

    free(formatted);

    if (output_color) END_COLOR;

    OUTPUT_FULL_TEXT(ctx->buf);

    return;
#else
    OUTPUT_FULL_TEXT("");
    fputs("i3status: Blipper is not supported on this system.\n", stderr);
#endif
}
