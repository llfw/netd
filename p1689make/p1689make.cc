/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of
 * this software dedicate any and all copyright interest in the software to the
 * public domain. We make this dedication for the benefit of the public at
 * large and to the detriment of our heirs and successors. We intend this
 * dedication to be an overt act of relinquishment in perpetuity of all present
 * and future rights to this software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Simple utility to convert P1689-format build dependencies to Makefile
 * format.
 */

#include	<print>
#include	<cstdio>
#include	<cstring>
#include	<cerrno>
#include	<string>
#include	<unordered_map>
#include	<optional>

#include	<private/ucl/ucl.h>

/*

{
  "revision": 0,
  "rules": [
    {
      "primary-output": "hello.o",
      "provides": [
        {
          "is-interface": true,
          "logical-name": "hello",
          "source-path": "hello.ccm"
        }
      ]
    },
    {
      "primary-output": "main.o",
      "requires": [
        {
          "logical-name": "hello",
          "source-path": "hello.ccm"
        }
      ]
    }
  ],
  "version": 1
}

*/

namespace {

struct p1689rule {
	std::string logical_name;
	std::string primary_output;
	std::string primary_basename;
	std::string source_path;
	std::string pcm_path;
};

std::unordered_map<std::string, p1689rule> rules;

auto add_rule(p1689rule rule) -> void {
	rules[rule.logical_name] = rule;
}

auto find_rule(std::string const &logical_name) -> std::optional<p1689rule> {
	if (auto it = rules.find(logical_name); it != rules.end())
		return it->second;
	return {};
}

auto process_rule(ucl_object_t const *rule) -> int {
	auto output = ucl_object_lookup(rule, "primary-output");
	if (!output) {
		std::print(stderr, "no output\n");
		return 0;
	}

	p1689rule r;
	r.primary_output = ucl_object_tostring(output);
	r.primary_basename = r.primary_output;
	if (auto pos = r.primary_basename.rfind('.'); pos != std::string::npos)
		r.primary_basename = r.primary_basename.substr(0, pos);

	std::print(stderr, "primary_basename = {}\n", r.primary_basename);

	// do requires
	auto reqs = ucl_object_lookup(rule, "requires");
	if (reqs) {
		std::print("{}:", ucl_object_tostring(output));

		auto reqit = ucl_object_iterate_new(reqs);
		ucl_object_t const *req;
		while ((req = ucl_object_iterate_safe(
					reqit, true)) != nullptr) {
			auto mod = ucl_object_lookup(req,
						     "logical-name");
			if (!mod) {
				std::print(stderr, "no mod\n");
				continue;
			}

			auto mod_ = ucl_object_tostring(mod);
			if (auto rule = find_rule(mod_); rule)
				std::print(" {}", rule->pcm_path);
			else
				std::print(stderr, "no rule for {}", mod_);
		}

		ucl_object_iterate_free(reqit);
		std::print("\n");
	}


	// do provides
	auto provides = ucl_object_lookup(rule, "provides");
	if (provides) {
		auto provit = ucl_object_iterate_new(provides);
		ucl_object_t const *prov;
		while ((prov = ucl_object_iterate_safe(
					provit, true)) != nullptr) {
			auto mod = ucl_object_lookup(prov,
						     "logical-name");
			if (!mod) {
				std::print(stderr, "no mod\n");
				continue;
			}

			auto src = ucl_object_lookup(prov,
						     "source-path");
			if (!src) {
				std::print(stderr, "no src\n");
				continue;
			}

			r.logical_name = ucl_object_tostring(mod);
			r.source_path = ucl_object_tostring(src);
			r.pcm_path = std::format("{}.pcm", r.primary_basename);
			add_rule(r);

			std::print("{0}: {1} {2}\n",
				   r.pcm_path,
				   ucl_object_tostring(output),
				   r.source_path);
		}

		ucl_object_iterate_free(provit);
	}

	return 0;
}

auto emit(ucl_object_t *top) -> int {
	auto rules = ucl_object_lookup(top, "rules");
	if (!rules) {
		std::print(stderr, "missing 'rules' object in JSON\n");
		return 1;
	}

	auto it = ucl_object_iterate_new(rules);
	while (auto rule = ucl_object_iterate_safe(it, true)) {
		if (process_rule(rule) == -1)
			return 1;
	}

	ucl_object_iterate_free(it);

	return 0;
}

} // anonymous namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::print(stderr, "usage: p1689make <input>\n");
		return 1;
	}

	auto parser = ucl_parser_new(0);
	if (!parser) {
		std::print(stderr, "ucl_parser_new: {}\n",
			   std::strerror(errno));
		return 1;
	}

	if (!ucl_parser_add_file(parser, argv[1])) {
		std::print(stderr, "ucl_parser_add_file: {}\n",
			   ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return 1;
	}

	auto object = ucl_parser_get_object(parser);

	auto ret = emit(object);

	ucl_object_unref(object);
	ucl_parser_free(parser);
	return ret;
}
