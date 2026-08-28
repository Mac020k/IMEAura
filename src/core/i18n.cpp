#include "core/i18n.h"

#include "core/firefly.h"
#include "core/input_languages.h"

#include <cstddef>

namespace imeaura {
namespace {

// clang-format off
const wchar_t* kJa[static_cast<size_t>(StringId::kCount)] = {
  L"IME Aura",
  L"Aura",
  L"Firefly",
  L"\u4E00\u822C",
  L"Aura",
  L"\u753B\u9762\u7E01\u306B\u5165\u529B\u8A00\u8A9E\u306B\u5FDC\u3058\u305F\u30B0\u30E9\u30C7\u30FC\u30B7\u30E7\u30F3\u3092\u8868\u793A\u3057\u307E\u3059",
  L"Aura \u3092\u6709\u52B9\u306B\u3059\u308B",
  L"\u8272",
  L"\u8A00\u8A9E\u3054\u3068\u306E\u753B\u9762\u7E01\u306E\u8272\u3092\u8A2D\u5B9A\u3057\u307E\u3059",
  L"\u65E5\u672C\u8A9E",
  L"\u82F1\u8A9E",
  L"\u30C7\u30D5\u30A9\u30EB\u30C8\u306E\u8272\u306B\u623B\u3059",
  L"\u623B\u3057\u307E\u3057\u305F",
  L"\u8272\u3092\u8FFD\u52A0",
  L"\u524A\u9664",
  L"\u30B0\u30E9\u30C7\u30FC\u30B7\u30E7\u30F3\u306E\u5E45",
  L"\u753B\u9762\u7E01\u306E\u5E2F\u306E\u539A\u3055 (1-100 px)",
  L"\u30C7\u30D5\u30A9\u30EB\u30C8\u306E\u5E45\u306B\u623B\u3059",
  L"\u623B\u3057\u307E\u3057\u305F",
  L"\u30B0\u30E9\u30C7\u30FC\u30B7\u30E7\u30F3\u8868\u793A",
  L"\u5E38\u306B\u8868\u793A",
  L"\u30C6\u30AD\u30B9\u30C8\u5165\u529B\u6642\u306E\u307F",
  L"\u975E\u8868\u793A",
  L"\u30C6\u30AD\u30B9\u30C8\u30DC\u30C3\u30AF\u30B9\u3078\u30DB\u30D0\u30FC\u6642\u3082\u8868\u793A",
  L"\u6587\u5B57\u30B5\u30A4\u30BA",
  L"\u3053\u306E\u30A6\u30A3\u30F3\u30C9\u30A6\u306E\u6587\u5B57\u306E\u5927\u304D\u3055",
  L"\u5C0F",
  L"\u4E2D",
  L"\u5927",
  L"\u8A00\u8A9E",
  L"\u8A00\u8A9E\u3092\u5909\u66F4",
  L"\u623B\u308B",
  L"\u65E5\u672C\u8A9E",
  L"English",
  L"\u7B80\u4F53\u4E2D\u6587",
  L"\u7E41\u9AD4\u4E2D\u6587",
  L"\uD55C\uAD6D\uC5B4",
  L"Easy Quit",
  L"\u30D0\u30FC\u30B8\u30E7\u30F3\u60C5\u5831...",
  L"\u30A2\u30D7\u30EA\u30B1\u30FC\u30B7\u30E7\u30F3\u3092\u7D42\u4E86",
  L"IME Aura",
  L"IME Aura \u3092\u7D42\u4E86\u3057\u307E\u3059\u304B\uFF1F\n\u753B\u9762\u7E01\u306E\u30B0\u30E9\u30C7\u30FC\u30B7\u30E7\u30F3\u8868\u793A\u3082\u6D88\u3048\u307E\u3059\u3002",
  L"Firefly",
  L"CapsLock\u30AD\u30FC\u306B\u7570\u306A\u308B\u6A5F\u80FD\u3092\u5272\u308A\u5F53\u3066\u308B\u3053\u3068\u3067\u3001\u5ACC\u308F\u308C\u3066\u3044\u308B\u30AD\u30FC\u3092\u4E00\u756A\u597D\u304D\u306A\u6C17\u306B\u3059\u308B\u3053\u3068\u304C\u3067\u304D\u307E\u3059\u3002\u5272\u308A\u5F53\u3066\u308B\u6A5F\u80FD\u306F\u4EE5\u4E0B\u306E\u30EA\u30B9\u30C8\u306E\u4E2D\u304B\u3089\u9078\u3093\u3067\u304F\u3060\u3055\u3044\u3002",
  L"Firefly \u6A5F\u80FD\u3092\u6709\u52B9\u306B\u3059\u308B",
  L"\u30A2\u30AF\u30C6\u30A3\u30D6",
  L"\u975E\u30A2\u30AF\u30C6\u30A3\u30D6",
  L"CapsLock \u306E\u72B6\u614B",
  L"\u6709\u52B9\u5316\u76F4\u524D\u306E\u72B6\u614B\u3092\u7DAD\u6301",
  L"\u5927\u6587\u5B57\u30D9\u30FC\u30B9",
  L"\u5C0F\u6587\u5B57\u30D9\u30FC\u30B9",
  L"CapsLock \u30AD\u30FC\u306E\u6A2A\u53D6\u308A",
  L"LED \u5236\u5FA1",
  L"\u901A\u77E5\u306E\u5FDC\u7B54\u4E0D\u53EF",
  L"\u3053\u306E\u74B0\u5883\u3067\u306F\u4E00\u90E8\u306E\u6A5F\u80FD\u304C\u5229\u7528\u3067\u304D\u307E\u305B\u3093",
  L"\u5272\u308A\u5F53\u3066\u308B\u6A5F\u80FD",
  L"\u6A5F\u80FD\u3092\u5909\u66F4",
  L"\u623B\u308B",
  L"\u901A\u77E5\u306E\u5FDC\u7B54\u4E0D\u53EF\u72B6\u614B\u3092\u5207\u308A\u66FF\u3048\u308B",
  L"PC \u3092\u30B9\u30EA\u30FC\u30D7\u3055\u305B\u306A\u3044",
  L"\u97F3\u58F0\u5165\u529B\u30B7\u30E7\u30FC\u30C8\u30AB\u30C3\u30C8",
  L"\u901A\u77E5\u306E\u5FDC\u7B54\u4E0D\u53EF + \u30DE\u30A4\u30AF Mute",
  L"\u901A\u77E5\u306E\u5FDC\u7B54\u4E0D\u53EF + \u97F3\u58F0\u5165\u529B\u30B7\u30E7\u30FC\u30C8\u30AB\u30C3\u30C8",
  L"\u753B\u9762\u3092\u9589\u3058\u306A\u3044",
  L"PC \u30B9\u30EA\u30FC\u30D7\u9632\u6B62",
  L"\u97F3\u58F0\u5165\u529B\u30B7\u30E7\u30FC\u30C8\u30AB\u30C3\u30C8",
  L"\u30DE\u30A4\u30AF Mute",
  L"\u8272\u3092\u9078\u629E",
  L"RGB",
  L"HSB",
  L"\u30D7\u30EA\u30BB\u30C3\u30C8",
  L"OK",
  L"\u30AD\u30E3\u30F3\u30BB\u30EB",
  L"\u8A2D\u5B9A\u3092\u958B\u304F",
  L"\u7D42\u4E86",
};

const wchar_t* kEn[static_cast<size_t>(StringId::kCount)] = {
  L"IME Aura",
  L"Aura",
  L"Firefly",
  L"General",
  L"Aura",
  L"Shows a screen-edge gradient for the active input language",
  L"Enable Aura",
  L"Color",
  L"Assign an edge color per input language",
  L"Japanese",
  L"English",
  L"Reset to default colors",
  L"Reset",
  L"Add color",
  L"Remove",
  L"Gradient Width",
  L"Screen edge thickness (1-100 px)",
  L"Reset to default width",
  L"Reset",
  L"Gradient Display",
  L"Always show",
  L"Only when text input is focused",
  L"Hidden",
  L"Also show when hovering over text boxes",
  L"Font Size",
  L"Text size in this window",
  L"S",
  L"M",
  L"L",
  L"Language",
  L"Change language",
  L"Back",
  L"\u65E5\u672C\u8A9E",
  L"English",
  L"\u7B80\u4F53\u4E2D\u6587",
  L"\u7E41\u9AD4\u4E2D\u6587",
  L"\uD55C\uAD6D\uC5B4",
  L"Easy Quit",
  L"About...",
  L"Quit Application",
  L"IME Aura",
  L"Quit IME Aura?\nThe screen edge gradient will also disappear.",
  L"Firefly",
  L"Assign a different function to the Caps Lock key and turn a disliked key into your favorite. Choose a function from the list below.",
  L"Enable Firefly",
  L"Active",
  L"Inactive",
  L"CapsLock state",
  L"Keep state before enabling",
  L"Uppercase base",
  L"Lowercase base",
  L"CapsLock interception",
  L"LED control",
  L"Notification Do Not Disturb",
  L"Some features are unavailable on this system",
  L"Function to assign",
  L"Change function",
  L"Back",
  L"Toggle notification Do Not Disturb",
  L"Prevent PC sleep",
  L"Voice typing shortcut",
  L"Do Not Disturb + mic mute",
  L"Do Not Disturb + voice typing",
  L"Keep display on",
  L"Prevent PC sleep",
  L"Voice typing shortcut",
  L"Microphone mute",
  L"Choose Color",
  L"RGB",
  L"HSB",
  L"Presets",
  L"OK",
  L"Cancel",
  L"Open Settings",
  L"Quit",
};

const wchar_t* kZhHans[static_cast<size_t>(StringId::kCount)] = {
  L"IME Aura",
  L"Aura",
  L"Firefly",
  L"\u4E00\u822C",
  L"Aura",
  L"\u6839\u636E\u5F53\u524D\u8F93\u5165\u8BED\u8A00\u5728\u5C4F\u5E55\u8FB9\u7F18\u663E\u793A\u6E10\u53D8",
  L"\u542F\u7528 Aura",
  L"\u989C\u8272",
  L"\u4E3A\u6BCF\u79CD\u8F93\u5165\u8BED\u8A00\u8BBE\u7F6E\u5C4F\u5E55\u8FB9\u7F18\u989C\u8272",
  L"\u65E5\u8BED",
  L"\u82F1\u8BED",
  L"\u6062\u590D\u9ED8\u8BA4\u989C\u8272",
  L"\u5DF2\u6062\u590D",
  L"\u6DFB\u52A0\u989C\u8272",
  L"\u5220\u9664",
  L"\u6E10\u53D8\u5BBD\u5EA6",
  L"\u5C4F\u5E55\u8FB9\u7F18\u539A\u5EA6 (1-100 px)",
  L"\u6062\u590D\u9ED8\u8BA4\u5BBD\u5EA6",
  L"\u5DF2\u6062\u590D",
  L"\u6E10\u53D8\u663E\u793A",
  L"\u59CB\u7EC8\u663E\u793A",
  L"\u4EC5\u5728\u6587\u672C\u8F93\u5165\u65F6",
  L"\u9690\u85CF",
  L"\u60AC\u505C\u5728\u6587\u672C\u6846\u4E0A\u65F6\u4E5F\u663E\u793A",
  L"\u5B57\u53F7",
  L"\u6B64\u7A97\u53E3\u4E2D\u7684\u6587\u5B57\u5927\u5C0F",
  L"\u5C0F",
  L"\u4E2D",
  L"\u5927",
  L"\u8BED\u8A00",
  L"\u66F4\u6539\u8BED\u8A00",
  L"\u8FD4\u56DE",
  L"\u65E5\u8BED",
  L"English",
  L"\u7B80\u4F53\u4E2D\u6587",
  L"\u7E41\u9AD4\u4E2D\u6587",
  L"\uD55C\uAD6D\uC5B4",
  L"Easy Quit",
  L"\u5173\u4E8E...",
  L"\u9000\u51FA\u5E94\u7528",
  L"IME Aura",
  L"\u9000\u51FA IME Aura\uFF1F\n\u5C4F\u5E55\u8FB9\u7F18\u6E10\u53D8\u4E5F\u4F1A\u6D88\u5931\u3002",
  L"Firefly",
  L"\u4E3A Caps Lock \u952E\u5206\u914D\u4E0D\u540C\u529F\u80FD\uFF0C\u8BA9\u4F60\u4E0D\u559C\u6B22\u7684\u952E\u53D8\u6210\u6700\u559C\u6B22\u7684\u952E\u3002\u8BF7\u4ECE\u4E0B\u65B9\u5217\u8868\u4E2D\u9009\u62E9\u8981\u5206\u914D\u7684\u529F\u80FD\u3002",
  L"\u542F\u7528 Firefly",
  L"\u6FC0\u6D3B",
  L"\u975E\u6FC0\u6D3B",
  L"CapsLock \u72B6\u6001",
  L"\u4FDD\u6301\u542F\u7528\u524D\u7684\u72B6\u6001",
  L"\u5927\u5199\u57FA\u51C6",
  L"\u5C0F\u5199\u57FA\u51C6",
  L"CapsLock \u62E6\u622A",
  L"LED \u63A7\u5236",
  L"\u901A\u77E5\u52FF\u6270",
  L"\u6B64\u73AF\u5883\u4E0B\u90E8\u5206\u529F\u80FD\u4E0D\u53EF\u7528",
  L"\u5206\u914D\u7684\u529F\u80FD",
  L"\u66F4\u6539\u529F\u80FD",
  L"\u8FD4\u56DE",
  L"\u5207\u6362\u901A\u77E5\u52FF\u6270\u72B6\u6001",
  L"\u9632\u6B62 PC \u4F11\u7720",
  L"\u8BED\u97F3\u8F93\u5165\u5FEB\u6377\u952E",
  L"\u52FF\u6270 + \u9EA6\u514B\u98CE\u9759\u97F3",
  L"\u52FF\u6270 + \u8BED\u97F3\u8F93\u5165",
  L"\u4FDD\u6301\u5C4F\u5E55\u5E38\u4EAE",
  L"\u9632\u6B62 PC \u4F11\u7720",
  L"\u8BED\u97F3\u8F93\u5165\u5FEB\u6377\u952E",
  L"\u9EA6\u514B\u98CE\u9759\u97F3",
  L"\u9009\u62E9\u989C\u8272",
  L"RGB",
  L"HSB",
  L"\u9884\u8BBE",
  L"\u786E\u5B9A",
  L"\u53D6\u6D88",
  L"\u6253\u5F00\u8BBE\u7F6E",
  L"\u9000\u51FA",
};

const wchar_t* kZhHant[static_cast<size_t>(StringId::kCount)] = {
  L"IME Aura",
  L"Aura",
  L"Firefly",
  L"\u4E00\u822C",
  L"Aura",
  L"\u4F9D\u64DA\u7576\u524D\u8F38\u5165\u8A9E\u8A00\u5728\u87A2\u5E55\u908A\u7DE3\u986F\u793A\u6F38\u5C64",
  L"\u555F\u7528 Aura",
  L"\u984F\u8272",
  L"\u70BA\u6BCF\u7A2E\u8F38\u5165\u8A9E\u8A00\u8A2D\u5B9A\u87A2\u5E55\u908A\u7DE3\u984F\u8272",
  L"\u65E5\u8A9E",
  L"\u82F1\u8A9E",
  L"\u6062\u5FA9\u9810\u8A2D\u984F\u8272",
  L"\u5DF2\u6062\u5FA9",
  L"\u65B0\u589E\u984F\u8272",
  L"\u522A\u9664",
  L"\u6F38\u5C64\u5BEC\u5EA6",
  L"\u87A2\u5E55\u908A\u7DE3\u539A\u5EA6 (1-100 px)",
  L"\u6062\u5FA9\u9810\u8A2D\u5BEC\u5EA6",
  L"\u5DF2\u6062\u5FA9",
  L"\u6F38\u5C64\u986F\u793A",
  L"\u59CB\u7D42\u986F\u793A",
  L"\u50C5\u5728\u6587\u5B57\u8F38\u5165\u6642",
  L"\u96B1\u85CF",
  L"\u6ED1\u9F20\u505C\u7559\u6587\u5B57\u6846\u6642\u4E5F\u986F\u793A",
  L"\u5B57\u578B\u5927\u5C0F",
  L"\u6B64\u8996\u7A97\u4E2D\u7684\u6587\u5B57\u5927\u5C0F",
  L"\u5C0F",
  L"\u4E2D",
  L"\u5927",
  L"\u8A9E\u8A00",
  L"\u8B8A\u66F4\u8A9E\u8A00",
  L"\u8FD4\u56DE",
  L"\u65E5\u8A9E",
  L"English",
  L"\u7B80\u4F53\u4E2D\u6587",
  L"\u7E41\u9AD4\u4E2D\u6587",
  L"\uD55C\uAD6D\uC5B4",
  L"Easy Quit",
  L"\u95DC\u65BC...",
  L"\u7D50\u675F\u61C9\u7528\u7A0B\u5F0F",
  L"IME Aura",
  L"\u7D50\u675F IME Aura\uFF1F\n\u87A2\u5E55\u908A\u7DE3\u6F38\u5C64\u4E5F\u6703\u6D88\u5931\u3002",
  L"Firefly",
  L"\u70BA Caps Lock \u9375\u5206\u914D\u4E0D\u540C\u529F\u80FD\uFF0C\u8B93\u4F60\u4E0D\u559C\u6B61\u7684\u9375\u8B8A\u6210\u6700\u559C\u6B61\u7684\u9375\u3002\u8ACB\u5F9E\u4E0B\u65B9\u5217\u8868\u4E2D\u9078\u64C7\u8981\u5206\u914D\u7684\u529F\u80FD\u3002",
  L"\u555F\u7528 Firefly",
  L"\u555F\u7528\u4E2D",
  L"\u975E\u555F\u7528",
  L"CapsLock \u72C0\u614B",
  L"\u4FDD\u7559\u555F\u7528\u524D\u7684\u72C0\u614B",
  L"\u5927\u5BEB\u57FA\u6E96",
  L"\u5C0F\u5BEB\u57FA\u6E96",
  L"CapsLock \u62E6\u622A",
  L"LED \u63A7\u5236",
  L"\u901A\u77E5\u52FF\u64FE",
  L"\u6B64\u74B0\u5883\u4E0B\u90E8\u5206\u529F\u80FD\u4E0D\u53EF\u7528",
  L"\u5206\u914D\u7684\u529F\u80FD",
  L"\u8B8A\u66F4\u529F\u80FD",
  L"\u8FD4\u56DE",
  L"\u5207\u63DB\u901A\u77E5\u52FF\u64FE\u72C0\u614B",
  L"\u9632\u6B62 PC \u7761\u7720",
  L"\u8A9E\u97F3\u8F38\u5165\u5FEB\u6377\u9375",
  L"\u52FF\u64FE + \u9EA5\u514B\u98A8\u975C\u97F3",
  L"\u52FF\u64FE + \u8A9E\u97F3\u8F38\u5165",
  L"\u4FDD\u6301\u87A2\u5E55\u5E38\u4EAE",
  L"\u9632\u6B62 PC \u7761\u7720",
  L"\u8A9E\u97F3\u8F38\u5165\u5FEB\u6377\u9375",
  L"\u9EA5\u514B\u98A8\u975C\u97F3",
  L"\u9078\u64C7\u984F\u8272",
  L"RGB",
  L"HSB",
  L"\u9810\u8A2D",
  L"\u78BA\u5B9A",
  L"\u53D6\u6D88",
  L"\u958B\u555F\u8A2D\u5B9A",
  L"\u7D50\u675F",
};

const wchar_t* kKo[static_cast<size_t>(StringId::kCount)] = {
  L"IME Aura",
  L"Aura",
  L"Firefly",
  L"\uC77C\uBC18",
  L"Aura",
  L"\uD65C\uC131 \uC785\uB825 \uC5B8\uC5B4\uC5D0 \uB9DE\uB294 \uD654\uBA74 \uAC00\uC7A5\uC790\uB9AC \uADF8\uB77C\uB370\uC774\uC158\uC744 \uD45C\uC2DC\uD569\uB2C8\uB2E4",
  L"Aura \uC0AC\uC6A9",
  L"\uC0C9\uC0C1",
  L"\uC785\uB825 \uC5B8\uC5B4\uBCC4 \uD654\uBA74 \uAC00\uC7A5\uC790\uB9AC \uC0C9\uC744 \uC124\uC815\uD569\uB2C8\uB2E4",
  L"\uC77C\uBCF8\uC5B4",
  L"\uC601\uC5B4",
  L"\uAE30\uBCF8 \uC0C9\uC73C\uB85C \uB418\uB3CC\uB9AC\uAE30",
  L"\uB418\uB3CC\uB9BC",
  L"\uC0C9 \uCD94\uAC00",
  L"\uC0AD\uC81C",
  L"\uADF8\uB77C\uB370\uC774\uC158 \uB108\uBE44",
  L"\uD654\uBA74 \uAC00\uC7A5\uC790\uB9AC \uB450\uAED8 (1-100 px)",
  L"\uAE30\uBCF8 \uB108\uBE44\uB85C \uB418\uB3CC\uB9AC\uAE30",
  L"\uB418\uB3CC\uB9BC",
  L"\uADF8\uB77C\uB370\uC774\uC158 \uD45C\uC2DC",
  L"\uD56D\uC0C1 \uD45C\uC2DC",
  L"\uD14D\uC2A4\uD2B8 \uC785\uB825 \uC911\uC5D0\uB9CC",
  L"\uC228\uAE40",
  L"\uD14D\uC2A4\uD2B8 \uC0C1\uC790\uC5D0 \uB9C8\uC6B0\uC2A4\uB97C \uC62C\uB824\uB3C4 \uD45C\uC2DC",
  L"\uAE00\uAF34 \uD06C\uAE30",
  L"\uC774 \uCC3D\uC758 \uAE00\uC790 \uD06C\uAE30",
  L"\uC18C",
  L"\uC911",
  L"\uB300",
  L"\uC5B8\uC5B4",
  L"\uC5B8\uC5B4 \uBCC0\uACBD",
  L"\uB4A4\uB85C",
  L"\uC77C\uBCF8\uC5B4",
  L"English",
  L"\u7B80\u4F53\u4E2D\u6587",
  L"\u7E41\u9AD4\u4E2D\u6587",
  L"\uD55C\uAD6D\uC5B4",
  L"Easy Quit",
  L"\uC815\uBCF4...",
  L"\uC571 \uC885\uB8CC",
  L"IME Aura",
  L"IME Aura\uB97C \uC885\uB8CC\uD560\uAE4C\uC694?\n\uD654\uBA74 \uAC00\uC7A5\uC790\uB9AC \uADF8\uB77C\uB370\uC774\uC158\uB3C4 \uC0AC\uB77C\uC9D1\uB2C8\uB2E4.",
  L"Firefly",
  L"Caps Lock \uD0A4\uC5D0 \uB2E4\uB978 \uAE30\uB2A5\uC744 \uD560\uB2F9\uD574 \uC2EB\uC5B4\uD558\uB358 \uD0A4\uB97C \uAC00\uC7A5 \uC88B\uC544\uD558\uB294 \uD0A4\uCC98\uB7FC \uB290\uB084 \uC218 \uC788\uC2B5\uB2C8\uB2E4. \uC544\uB798 \uBAA9\uB85D\uC5D0\uC11C \uD560\uB2F9\uD560 \uAE30\uB2A5\uC744 \uC120\uD0DD\uD558\uC138\uC694.",
  L"Firefly \uC0AC\uC6A9",
  L"\uD65C\uC131",
  L"\uBE44\uD65C\uC131",
  L"CapsLock \uC0C1\uD0DC",
  L"\uD65C\uC131\uD654 \uC804 \uC0C1\uD0DC \uC720\uC9C0",
  L"\uB300\uBB38\uC790 \uAE30\uC900",
  L"\uC18C\uBB38\uC790 \uAE30\uC900",
  L"CapsLock \uC778\uD130\uC149\uD2B8",
  L"LED \uC81C\uC5B4",
  L"\uC54C\uB9BC \uBC29\uD574 \uAE08\uC9C0",
  L"\uC774 \uD658\uACBD\uC5D0\uC11C\uB294 \uC77C\uBD80 \uAE30\uB2A5\uC744 \uC0AC\uC6A9\uD560 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4",
  L"\uD560\uB2F9\uD560 \uAE30\uB2A5",
  L"\uAE30\uB2A5 \uBCC0\uACBD",
  L"\uB4A4\uB85C",
  L"\uC54C\uB9BC \uBC29\uD574 \uAE08\uC9C0 \uC0C1\uD0DC \uC804\uD658",
  L"PC \uC808\uC804 \uBC29\uC9C0",
  L"\uC74C\uC131 \uC785\uB825 \uB2E8\uCD95\uD0A4",
  L"\uBC29\uD574 \uAE08\uC9C0 + \uB9C8\uC774\uD06C \uC74C\uC18C\uAC70",
  L"\uBC29\uD574 \uAE08\uC9C0 + \uC74C\uC131 \uC785\uB825",
  L"\uD654\uBA74 \uB04C\uC9C0 \uC54A\uAE30",
  L"PC \uC808\uC804 \uBC29\uC9C0",
  L"\uC74C\uC131 \uC785\uB825 \uB2E8\uCD95\uD0A4",
  L"\uB9C8\uC774\uD06C \uC74C\uC18C\uAC70",
  L"\uC0C9 \uC120\uD0DD",
  L"RGB",
  L"HSB",
  L"\uD504\uB9AC\uC14B",
  L"\uD655\uC778",
  L"\uCDE8\uC18C",
  L"\uC124\uC815 \uC5F4\uAE30",
  L"\uC885\uB8CC",
};
// clang-format on

const wchar_t* const* table_for(Lang lang) {
  switch (lang) {
    case Lang::En:
      return kEn;
    case Lang::ZhHans:
      return kZhHans;
    case Lang::ZhHant:
      return kZhHant;
    case Lang::Ko:
      return kKo;
    case Lang::Ja:
    default:
      return kJa;
  }
}

}  // namespace

const wchar_t* tr(Lang lang, StringId id) {
  const auto idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(StringId::kCount)) return L"";
  const wchar_t* const* table = table_for(lang);
  if (table[idx] && table[idx][0] != L'\0') return table[idx];
  return kEn[idx] ? kEn[idx] : L"";
}

Lang lang_from_key(const std::string& key) {
  if (key == kInputEn) return Lang::En;
  if (key == kInputZhHans) return Lang::ZhHans;
  if (key == kInputZhHant) return Lang::ZhHant;
  if (key == kInputKo) return Lang::Ko;
  if (key == kInputJa) return Lang::Ja;
  return Lang::En;
}

const wchar_t* lang_font_family(Lang lang) {
  switch (lang) {
    case Lang::En:
      return L"Segoe UI";
    case Lang::ZhHans:
      return L"Microsoft YaHei UI";
    case Lang::ZhHant:
      return L"Microsoft JhengHei UI";
    case Lang::Ko:
      return L"Malgun Gothic";
    case Lang::Ja:
    default:
      return L"Yu Gothic UI";
  }
}

StringId string_id_for_ui_lang(const std::string& key) {
  if (key == kInputEn) return StringId::kLangEn;
  if (key == kInputZhHans) return StringId::kLangZhHans;
  if (key == kInputZhHant) return StringId::kLangZhHant;
  if (key == kInputKo) return StringId::kLangKo;
  return StringId::kLangJa;
}

StringId string_id_for_busy_action(const std::string& action_key) {
  const std::string a = normalize_busy_action(action_key);
  if (a == kFireflyBusyKeepAwake) return StringId::kFireflyBusyKeepAwake;
  if (a == kFireflyBusyVoiceInput) return StringId::kFireflyBusyVoiceInput;
  if (a == kFireflyBusyMeeting) return StringId::kFireflyBusyMeeting;
  if (a == kFireflyBusyHandsFree) return StringId::kFireflyBusyHandsFree;
  return StringId::kFireflyBusyDnd;
}

}  // namespace imeaura
