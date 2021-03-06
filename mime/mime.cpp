//
//  .__  ._____.
//  |  | |__\_ |__
//  |  | |  || __ \
//  |  |_|  || \_\ \     .__
//  |____/__||___  /____ |__| _____   ____
//               \/     \|  |/     \_/ __ \
//               |  Y Y  \  |  Y Y  \  ___/
//               |__|_|  /__|__|_|  /\___  >
//                     \/         \/     \/
//
//  libmime
//  A C++ library for inferring MIME content-types from pathnames
//
//  Copyright © 2018 D.E. Goodman-Wilson
//

#include <unordered_map>
#include <vector>
#include <fstream>
#include <regex>
#include <nlohmann/json.hpp>
#include "mime.h"
#include "db.h"

// taken straight from https://github.com/jshttp/mime-types/blob/master/index.js

namespace mime
{

namespace private_
{

static bool inited_{false};

struct content_type_t_
{
    enum class source_t
    {
        APACHE,
        IANA,
        NGINX,
        UNKNOWN,
    } source;
    std::vector <std::string> extensions;
    bool compressible;
    std::string charset;

    content_type_t_() : source{source_t::UNKNOWN}, extensions{}, compressible{false}, charset{""}
    {}
};

void from_json(const nlohmann::json &j, content_type_t_ &c)
{
    try
    {
        std::string source = j.at("source").get<std::string>();
        std::transform(source.begin(), source.end(), source.begin(), ::tolower);

        if (source == "apache")
        { c.source = content_type_t_::source_t::APACHE; }
        else if (source == "iana")
        { c.source = content_type_t_::source_t::IANA; }
        else if (source == "nginx") c.source = content_type_t_::source_t::NGINX;
    }
    catch (const nlohmann::json::exception &e)
    {}

    try
    {
        c.extensions = j.at("extensions").get < std::vector < std::string >> ();
    }
    catch (const nlohmann::json::exception &e)
    {}

    try
    {
        c.compressible = j.at("compressible").get<bool>();
    }
    catch (const nlohmann::json::exception &e)
    {}

    try
    {
        c.charset = j.at("charset").get<std::string>();
    }
    catch (const nlohmann::json::exception &e)
    {}
}

static std::unordered_map <std::string, content_type_t_> db_;
static std::unordered_map <std::string, std::vector<std::string>> extensions_;
static std::unordered_map <std::string, std::string> types_;

static const std::regex EXTRACT_TYPE_REGEXP{R"(^\s*([^;\s]*)(?:;|\s|$))", std::regex_constants::ECMAScript};
static const std::regex TEXT_TYPE_REGEXP{"^text", std::regex_constants::ECMAScript};


std::string ext_from_filename_(std::string filename)
{
    if (filename.empty()) return "";

    auto dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos) return ""; // no extension on names with a . like foobar

    return filename.substr(dot_pos + 1);
}

std::string ext_from_path_(std::string path)
{
    auto filename(path);
    // strip off any leadings paths.
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos)
    {
        filename = path.substr(pos + 1);
    }
    else
    {
        pos = path.find_last_of('\\');
        if (pos != std::string::npos)
        {
            filename = path.substr(pos + 1);
        }
    }
    auto dot_pos = filename.find_last_of('.');
    if (dot_pos == 0) return ""; // no extension on names with a . like foobar

    return ext_from_filename_(filename);
}
}

void init(void)
{
    if (private_::inited_) return;

    nlohmann::json db = nlohmann::json::parse(db_json);

    //TOOD eliminate this
    private_::db_ = db.get < std::unordered_map < std::string, private_::content_type_t_ >> ();


    // source preference (least -> most)
    std::vector <private_::content_type_t_::source_t> preference{private_::content_type_t_::source_t::NGINX,
                                                             private_::content_type_t_::source_t::APACHE,
                                                             private_::content_type_t_::source_t::UNKNOWN,
                                                             private_::content_type_t_::source_t::IANA};

    for (const auto &kv : private_::db_)
    {
        auto type = kv.first;
        auto mime = kv.second;

        auto exts = mime.extensions;

        if (exts.empty())
        {
            continue;
        }

        // mime -> extensions
        private_::extensions_[type] = exts;

        // extension -> mime
        for (const auto &extension : exts)
        {
            if (private_::types_.count(extension))
            {
                auto from = std::find(std::begin(preference),
                                      std::end(preference),
                                      private_::db_.at(private_::types_.at(extension)).source);
                auto to = std::find(std::begin(preference), std::end(preference), mime.source);
                if (private_::types_.at(extension) != "application/octet-stream" &&
                    (from > to || (from == to && private_::types_.at(extension).substr(0, 12) == "application/")))
                {
                    // skip the remapping
                    continue;
                }
            }

            // set the extension -> mime
            private_::types_[extension] = type;
        }
    }

    private_::inited_ = true;
}

std::string get_extension_from_path(std::string str)
{
    init();

    std::transform(str.begin(), str.end(), str.begin(), ::tolower);

    //is this a _path_, or a _filename_, or an _extension_

    if (str.find('/') != std::string::npos || str.find('\\') != std::string::npos)
    {
        return private_::ext_from_path_(str);
    }

    if (str.find('.') != std::string::npos)
    {
        return private_::ext_from_filename_(str);
    }

    // it's just an extension, use it directly!
    return str;
}

std::string lookup(const std::string &str)
{
    init();

    auto extension = get_extension_from_path(str);

    if (extension.empty())
    {
        throw std::out_of_range{"mime::lookup: unknown extension"};
    };

    try
    {
        return private_::types_.at(extension);
    }
    catch(...)
    {
        throw std::out_of_range{"mime::lookup: unknown extension"};
    }
}


std::string content_type(const std::string &str)
{
    init();

    std::string mime{str};
    if (str.find('/') == std::string::npos)
    {
        mime = lookup(str);
    }

    if (mime.empty())
    {
        throw std::out_of_range{"mime::content_type: invalid content-type"};
    }

    // TODO: use content-type or other module
    if (mime.find("charset") == std::string::npos)
    {
        std::string cs{""};
        try
        {
            cs = charset(mime);
        }
        catch (const std::out_of_range &e)
        {}

        if (!cs.empty())
        {
            std::transform(cs.begin(), cs.end(), cs.begin(), ::tolower);
            mime += "; charset=" + cs;
        }
    }

    return mime;
}


std::string extension(const std::string &type)
{
    init();

    // TODO: use media-typer
    std::smatch matches;

    // get extensions
    std::vector <std::string> exts;
    std::regex_search(type, matches, private_::EXTRACT_TYPE_REGEXP);
    std::string match{matches[1].str()};
    std::transform(match.begin(), match.end(), match.begin(), ::tolower);
    try
    {
        return private_::extensions_.at(match)[0];
    }
    catch(...)
    {
        throw std::out_of_range("mime::extension: unknown extension");
    }
}


std::string charset(const std::string &type)
{
    init();

    // TODO: use media-typer
    std::smatch matches;

    // get extensions
    private_::content_type_t_ mime;
    auto did_match = std::regex_search(type, matches, private_::EXTRACT_TYPE_REGEXP);
    std::string match;
    if (did_match)
    {
        match = matches[1].str();
        std::transform(match.begin(), match.end(), match.begin(), ::tolower);
        if (private_::db_.count(match))
        {
            mime = private_::db_.at(match);
        }
    }

    if (!mime.charset.empty())
    {
        return mime.charset;
    }

    // default text/* to utf-8
    if (did_match && std::regex_search(match, private_::TEXT_TYPE_REGEXP))
    {
        return "UTF-8";
    }

    throw std::out_of_range("mime::charset: unknown extension");
}

}
