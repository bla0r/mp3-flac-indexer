#include <taglib/fileref.h>
#include <taglib/tag.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

static std::string trim(std::string s) {
  auto notspace = [](unsigned char c){ return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
  return s;
}

static std::string to_lower(std::string s) {
  for (auto &ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

static std::vector<std::string> split_csv(const std::string &s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = trim(item);
    if (!item.empty()) out.push_back(to_lower(item));
  }
  return out;
}

static bool parse_bool(std::string s, bool def=false) {
  s = to_lower(trim(s));
  if (s.empty()) return def;
  return (s == "1" || s == "true" || s == "yes" || s == "on");
}

static std::string sanitize_component(std::string s) {
  s = trim(s);
  if (s.empty()) return "Unknown";

  // Replace path separators and problematic characters.
  for (auto &ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '\\' || ch == '\0' || ch == ':') ch = '_';
    else if (c < 32) ch = '_';
  }

  // Collapse consecutive spaces/underscores.
  std::string out;
  out.reserve(s.size());
  char prev = 0;
  for (char ch : s) {
    if ((ch == ' ' || ch == '_') && (prev == ' ' || prev == '_')) continue;
    out.push_back(ch);
    prev = ch;
  }

  out = trim(out);
  if (out.empty()) out = "Unknown";
  return out;
}

struct Config {
  // Backwards compatible: MUSIC_DIR can be used for both types.
  std::vector<fs::path> music_dirs;
  // Prefer these when set.
  std::vector<fs::path> mp3_dirs;
  std::vector<fs::path> flac_dirs;
  fs::path index_root;
  bool relative_symlinks = false;
  bool clean_on_start = false;
  bool follow_symlinks = false;
  std::vector<std::string> enable_types = {"mp3", "flac"};
  std::vector<std::string> mp3_indexes = {"alpha", "genre", "year", "groups"};
  std::vector<std::string> flac_indexes = {"alpha", "genre", "groups", "year"};

  // How many directory levels below the scan root define a "release".
  // Example layout: /site/recent/mp3/YYYY-MM-DD/<release>/... => depth=2
  int mp3_release_depth = 1;
  int flac_release_depth = 1;
};

static Config load_config(const fs::path &cfg_path) {
  std::ifstream in(cfg_path);
  if (!in) {
    throw std::runtime_error("Cannot open config file: " + cfg_path.string());
  }

  std::unordered_map<std::string, std::vector<std::string>> kv;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line[0] == '#') continue;

    auto pos = line.find('=');
    if (pos == std::string::npos) continue;

    std::string key = to_lower(trim(line.substr(0, pos)));
    std::string val = trim(line.substr(pos + 1));
    if (key.empty()) continue;
    kv[key].push_back(val);
  }

  Config cfg;

  // MUSIC_DIR can repeat
  if (kv.count("music_dir")) {
    for (const auto &v : kv["music_dir"]) {
      if (!trim(v).empty()) cfg.music_dirs.emplace_back(v);
    }
  }

  // MP3_DIR / FLAC_DIR can repeat (preferred over MUSIC_DIR)
  if (kv.count("mp3_dir")) {
    for (const auto &v : kv["mp3_dir"]) {
      if (!trim(v).empty()) cfg.mp3_dirs.emplace_back(v);
    }
  }
  if (kv.count("flac_dir")) {
    for (const auto &v : kv["flac_dir"]) {
      if (!trim(v).empty()) cfg.flac_dirs.emplace_back(v);
    }
  }

  if (cfg.music_dirs.empty() && cfg.mp3_dirs.empty() && cfg.flac_dirs.empty()) {
    throw std::runtime_error("Config error: at least one MUSIC_DIR=... or MP3_DIR=... or FLAC_DIR=... is required");
  }

  if (kv.count("index_root") && !kv["index_root"].empty()) {
    cfg.index_root = fs::path(kv["index_root"].back());
  } else {
    throw std::runtime_error("Config error: INDEX_ROOT=... is required");
  }

  if (kv.count("relative_symlinks") && !kv["relative_symlinks"].empty())
    cfg.relative_symlinks = parse_bool(kv["relative_symlinks"].back(), false);

  if (kv.count("clean_on_start") && !kv["clean_on_start"].empty())
    cfg.clean_on_start = parse_bool(kv["clean_on_start"].back(), false);

  if (kv.count("follow_symlinks") && !kv["follow_symlinks"].empty())
    cfg.follow_symlinks = parse_bool(kv["follow_symlinks"].back(), false);

  if (kv.count("enable_types") && !kv["enable_types"].empty())
    cfg.enable_types = split_csv(kv["enable_types"].back());

  if (kv.count("mp3_indexes") && !kv["mp3_indexes"].empty())
    cfg.mp3_indexes = split_csv(kv["mp3_indexes"].back());

  if (kv.count("flac_indexes") && !kv["flac_indexes"].empty())
    cfg.flac_indexes = split_csv(kv["flac_indexes"].back());

  auto parse_int = [](const std::string &s, int def) {
    try {
      std::string t = trim(s);
      if (t.empty()) return def;
      int v = std::stoi(t);
      return (v <= 0) ? def : v;
    } catch (...) {
      return def;
    }
  };

  if (kv.count("mp3_release_depth") && !kv["mp3_release_depth"].empty())
    cfg.mp3_release_depth = parse_int(kv["mp3_release_depth"].back(), cfg.mp3_release_depth);

  if (kv.count("flac_release_depth") && !kv["flac_release_depth"].empty())
    cfg.flac_release_depth = parse_int(kv["flac_release_depth"].back(), cfg.flac_release_depth);

  return cfg;
}

struct ReleaseInfo {
  fs::path release_dir;
  std::string release_name;
  std::string artist;
  std::string album;
  std::string genre;
  std::string year;
  std::string group;
  char alpha;
};

static std::string taglib_string_to_utf8(const TagLib::String &s) {
  // true => UTF-8
  const char* p = s.toCString(true);
  return p ? std::string(p) : std::string{};
}


static std::optional<ReleaseInfo> read_release_info(const fs::path &audio_file,
                                                    const fs::path &release_dir) {
  TagLib::FileRef f(audio_file.c_str());
  if (f.isNull() || !f.tag()) return std::nullopt;

  TagLib::Tag *t = f.tag();

  ReleaseInfo info;
  info.release_dir = release_dir;
  info.release_name = info.release_dir.filename().string();

  info.artist = sanitize_component(taglib_string_to_utf8(t->artist()));
  info.album  = sanitize_component(taglib_string_to_utf8(t->album()));

  std::string genre = taglib_string_to_utf8(t->genre());
  info.genre = sanitize_component(genre);

  unsigned int y = t->year();
  info.year = (y > 0) ? std::to_string(y) : "Unknown";

  // group: substring after last '-' in release_name
  auto pos = info.release_name.find_last_of('-');
  if (pos != std::string::npos && pos + 1 < info.release_name.size()) {
    info.group = sanitize_component(info.release_name.substr(pos + 1));
  } else {
    info.group = "Unknown";
  }

  // alpha: first character (A-Z, 0-9); otherwise '#'
  char c = info.release_name.empty() ? '#' : info.release_name[0];
  c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  info.alpha = (std::isalnum(static_cast<unsigned char>(c)) ? c : '#');

  return info;
}

static void ensure_dir(const fs::path &p, bool dry_run) {
  if (dry_run) return;
  std::error_code ec;
  fs::create_directories(p, ec);
  if (ec) {
    throw std::runtime_error("Cannot create directory: " + p.string() + " (" + ec.message() + ")");
  }
}

static bool create_or_replace_symlink(const fs::path &target_abs,
                                     const fs::path &link_path,
                                     bool relative,
                                     bool force,
                                     bool dry_run) {
  std::error_code ec;
  fs::path link_parent = link_path.parent_path();

  fs::path target = target_abs;
  if (relative) {
    target = fs::relative(target_abs, link_parent, ec);
    if (ec) {
      // fallback to absolute
      target = target_abs;
      ec.clear();
    }
  }

  if (fs::exists(link_path, ec)) {
    if (!force) return false;
    if (!dry_run) {
      fs::remove(link_path, ec);
      if (ec) {
        throw std::runtime_error("Cannot remove existing link: " + link_path.string() + " (" + ec.message() + ")");
      }
    }
  }

  if (dry_run) return true;

  fs::create_symlink(target, link_path, ec);
  if (ec) {
    throw std::runtime_error("symlink failed: " + link_path.string() + " -> " + target.string() + " (" + ec.message() + ")");
  }
  return true;
}

static void clean_index_tree(const fs::path &base, bool dry_run) {
  if (dry_run) return;
  std::error_code ec;
  if (fs::exists(base, ec)) {
    for (auto it = fs::directory_iterator(base, ec); !ec && it != fs::directory_iterator(); ++it) {
      fs::remove_all(it->path(), ec);
      if (ec) break;
    }
  }
  if (ec) {
    throw std::runtime_error("Failed to clean index tree: " + base.string() + " (" + ec.message() + ")");
  }
}

static void index_release(const std::string &type,
                          const ReleaseInfo &info,
                          const Config &cfg,
                          const std::vector<std::string> &indexes,
                          bool force,
                          bool dry_run) {
  fs::path type_root = cfg.index_root / type;

  auto add_index = [&](const std::string &idx_name, const fs::path &subdir) {
    fs::path base = type_root / idx_name / subdir;
    ensure_dir(base, dry_run);
    fs::path link = base / info.release_name;
    fs::path target = fs::absolute(info.release_dir);

    (void)create_or_replace_symlink(target, link, cfg.relative_symlinks, force, dry_run);
  };

  for (const auto &idx : indexes) {
    if (idx == "alpha") {
      std::string a(1, info.alpha);
      add_index("alpha", fs::path(a));
    } else if (idx == "genre") {
      add_index("genre", fs::path(info.genre));
    } else if (idx == "year") {
      add_index("year", fs::path(info.year));
    } else if (idx == "artist") {
      add_index("artist", fs::path(info.artist));
    } else if (idx == "album") {
      add_index("album", fs::path(info.album));
    } else if (idx == "groups" || idx == "group") {
      add_index("groups", fs::path(info.group));
    }
  }
}

static bool has_ext(const fs::path &p, const std::string &ext_lower) {
  std::string e = to_lower(p.extension().string());
  return e == ext_lower;
}

static fs::path compute_release_dir(const fs::path &root,
                                   const fs::path &file_path,
                                   int release_depth) {
  // Determine release dir relative to root: take the first `release_depth` components
  // of the file's parent path (relative to root). This supports layouts like:
  //   root/YYYY-MM-DD/<release>/.../track.mp3  => depth=2
  //   root/<release>/track.flac               => depth=1
  fs::path rel = file_path.lexically_relative(root);
  fs::path rel_parent = rel.parent_path();
  if (rel_parent.empty()) return file_path.parent_path();

  fs::path acc;
  int count = 0;
  for (auto it = rel_parent.begin(); it != rel_parent.end(); ++it) {
    acc /= *it;
    ++count;
    if (count >= release_depth) break;
  }

  // If there were fewer components than release_depth, just use the full parent.
  if (count < release_depth) acc = rel_parent;
  return root / acc;
}

static void run_for_type(const std::string &type,
                         const Config &cfg,
                         const std::vector<std::string> &indexes,
                         bool force,
                         bool clean,
                         bool dry_run) {
  const std::string ext = (type == "mp3") ? ".mp3" : ".flac";
  const int release_depth = (type == "mp3") ? cfg.mp3_release_depth : cfg.flac_release_depth;

  const std::vector<fs::path> &roots =
      (type == "mp3") ? (cfg.mp3_dirs.empty() ? cfg.music_dirs : cfg.mp3_dirs)
                     : (cfg.flac_dirs.empty() ? cfg.music_dirs : cfg.flac_dirs);

  fs::path type_root = cfg.index_root / type;
  if (clean) {
    // Clean only the categories we will touch.
    for (const auto &idx : indexes) {
      std::string cat = idx;
      if (cat == "group") cat = "groups";
      fs::path base = type_root / cat;
      clean_index_tree(base, dry_run);
    }
  }

  std::unordered_set<std::string> seen_release_dirs;

  fs::directory_options opts = fs::directory_options::skip_permission_denied;
  if (cfg.follow_symlinks)
    opts |= fs::directory_options::follow_directory_symlink;

  std::size_t files_seen = 0;
  std::size_t releases_indexed = 0;

  for (const auto &root : roots) {
    std::error_code ec;
    if (!fs::exists(root, ec)) {
      std::cerr << "[warn] scan root does not exist: " << root << "\n";
      continue;
    }

    for (fs::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
      if (ec) {
        ec.clear();
        continue;
      }
      const fs::directory_entry &ent = *it;
      if (!ent.is_regular_file(ec)) continue;
      const fs::path &p = ent.path();
      if (!has_ext(p, ext)) continue;

      ++files_seen;
      fs::path release_dir = compute_release_dir(root, p, release_depth);
      std::string release_key = fs::absolute(release_dir, ec).string();
      if (!ec && seen_release_dirs.count(release_key)) continue;

      auto info_opt = read_release_info(p, release_dir);
      if (!info_opt) continue;

      if (!ec) seen_release_dirs.insert(release_key);

      index_release(type, *info_opt, cfg, indexes, force, dry_run);
      ++releases_indexed;
    }
  }

  std::cerr << "[" << type << "] scanned files: " << files_seen << ", indexed releases: " << releases_indexed << "\n";
}

static void print_usage(const char *argv0) {
  std::cerr
    << "Usage: " << argv0 << " <config> [--dry-run] [--force] [--clean]\n"
    << "\nConfig keys:\n"
    << "  MUSIC_DIR=/path (repeatable, fallback for both types)\n"
    << "  MP3_DIR=/path (repeatable, preferred for mp3)\n"
    << "  FLAC_DIR=/path (repeatable, preferred for flac)\n"
    << "  INDEX_ROOT=/index\n"
    << "  ENABLE_TYPES=mp3,flac\n"
    << "  MP3_INDEXES=alpha,genre,year,groups\n"
    << "  FLAC_INDEXES=alpha,genre,groups,year\n"
    << "  MP3_RELEASE_DEPTH=1 (example: root/YYYY-MM-DD/<release>/... => 2)\n"
    << "  FLAC_RELEASE_DEPTH=1 (example: root/YYYY-MM-DD/<release>/... => 2)\n"
    << "  (Also supported index names: artist, album)\n"
    << "  RELATIVE_SYMLINKS=true|false\n"
    << "  CLEAN_ON_START=true|false\n"
    << "  FOLLOW_SYMLINKS=true|false\n";
}

int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      print_usage(argv[0]);
      return 2;
    }

    fs::path cfg_path = argv[1];

    bool dry_run = false;
    bool force = false;
    bool clean_override = false;
    bool clean_flag = false;

    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--dry-run") dry_run = true;
      else if (a == "--force") force = true;
      else if (a == "--clean") { clean_override = true; clean_flag = true; }
      else if (a == "--no-clean") { clean_override = true; clean_flag = false; }
      else if (a == "--help" || a == "-h") { print_usage(argv[0]); return 0; }
      else {
        std::cerr << "Unknown arg: " << a << "\n";
        print_usage(argv[0]);
        return 2;
      }
    }

    Config cfg = load_config(cfg_path);

    bool clean = clean_override ? clean_flag : cfg.clean_on_start;

    std::unordered_set<std::string> enabled(cfg.enable_types.begin(), cfg.enable_types.end());

    if (enabled.count("mp3")) {
      run_for_type("mp3", cfg, cfg.mp3_indexes, force, clean, dry_run);
    }
    if (enabled.count("flac")) {
      run_for_type("flac", cfg, cfg.flac_indexes, force, clean, dry_run);
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
