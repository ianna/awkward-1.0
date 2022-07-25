// BSD 3-Clause License; see https://github.com/scikit-hep/awkward-1.0/blob/main/LICENSE

#ifndef AWKWARD_LAYOUTBUILDER_H_
#define AWKWARD_LAYOUTBUILDER_H_

#include "awkward/GrowableBuffer.h"
#include "awkward/utils.h"

#include <map>
#include <algorithm>
#include <tuple>
#include <string>

namespace awkward {

  namespace LayoutBuilder {

  // helper class for RecordLayoutBuilder
  template<std::size_t ENUM, typename BUILDER>
  class Field {
  public:
    using Builder = BUILDER;

    std::string index_as_field() const {
      return std::to_string(static_cast<size_t>(index));
    }

    const std::size_t index = ENUM;
    Builder builder;
  };

  // NumpyLayoutBuilder
  template <unsigned INITIAL, typename PRIMITIVE>
  class Numpy {
  public:
    Numpy()
        : data_(awkward::GrowableBuffer<PRIMITIVE>(INITIAL)) {
      size_t id = 0;
      set_id(id);
    }

    void
    append(PRIMITIVE x) noexcept {
      data_.append(x);
    }

    void
    extend(PRIMITIVE* ptr, size_t size) noexcept {
      data_.extend(ptr, size);
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
    }

    void
    clear() noexcept {
      data_.clear();
    }

    size_t
    length() const noexcept {
      return data_.length();
    }

    bool is_valid(std::string& error) const noexcept {
      return true;
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      names_nbytes["node" + std::to_string(id_) + "-data"] = data_.nbytes();
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      data_.concatenate(static_cast<PRIMITIVE*>(buffers["node" + std::to_string(id_) + "-data"]));
    }

    // temporary
    void
    to_buffer(PRIMITIVE* ptr) const noexcept {
      data_.concatenate(ptr);
    }

    std::string
    form() const {
      std::stringstream form_key;
      form_key << "node" << id_;

      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }

      if (std::is_arithmetic<PRIMITIVE>::value) {
        return "{ \"class\": \"NumpyArray\", \"primitive\": \""
                  + type_to_name<PRIMITIVE>() +"\"" + params
                  + ", \"form_key\": \"" + form_key.str() + "\" }";
      }
      else if (is_specialization<PRIMITIVE, std::complex>::value) {
        return "{ \"class\": \"NumpyArray\", \"primitive\": \""
                  + type_to_name<PRIMITIVE>() + "\"" + params
                  + ", \"form_key\": \"" + form_key.str() + "\" }";
      }
      else {
        throw std::runtime_error
          ("type " + std::string(typeid(PRIMITIVE).name()) + "is not supported");
      }
    }

  private:
    awkward::GrowableBuffer<PRIMITIVE> data_;
    std::string parameters_;
    size_t id_;
  };

  // ListOffsetLayoutBuilder
  template <unsigned INITIAL, typename PRIMITIVE, typename BUILDER>
  class ListOffset {
  public:
    ListOffset()
        : offsets_(awkward::GrowableBuffer<PRIMITIVE>(INITIAL)) {
      offsets_.append(0);
      size_t id = 0;
      set_id(id);
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    BUILDER&
    begin_list() noexcept {
      return content_;
    }

    void
    end_list() noexcept {
      offsets_.append(content_.length());
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      offsets_.clear();
      offsets_.append(0);
      content_.clear();
    }

    size_t
    length() const noexcept {
      return offsets_.length() - 1;
    }

    bool is_valid(std::string& error) const noexcept {
      if (content_.length() != offsets_.last()) {
        std::stringstream out;
        out << "ListOffset node" << id_ << "has content length " << content_.length()
            << "but last offset " << offsets_.last() << "\n";
        error.append(out.str());

        return false;
      }
      else {
        return content_.is_valid(error);
      }
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      names_nbytes["node" + std::to_string(id_) + "-offsets"] = offsets_.nbytes();
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      offsets_.concatenate(static_cast<PRIMITIVE*>(buffers["node" + std::to_string(id_) + "-offsets"]));
      content_.to_buffers(buffers);
    }

    // temporary
    void
    to_buffer(PRIMITIVE* ptr) const noexcept {
      offsets_.concatenate(ptr);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"ListOffsetArray\", \"offsets\": \""
                + type_to_numpy_like<PRIMITIVE>()
                + "\", \"content\": "+ content_.form() + params
                + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    GrowableBuffer<PRIMITIVE> offsets_;
    BUILDER content_;
    std::string parameters_;
    size_t id_;
  };

  // ListLayoutBuilder
  template <unsigned INITIAL, typename PRIMITIVE, typename BUILDER>
  class List {
  public:
    List()
        : starts_(awkward::GrowableBuffer<PRIMITIVE>(INITIAL))
        , stops_(awkward::GrowableBuffer<PRIMITIVE>(INITIAL)) {
      size_t id = 0;
      set_id(id);
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    BUILDER&
    begin_list() noexcept {
      starts_.append(content_.length());
      return content_;
    }

    void
    end_list() noexcept {
      stops_.append(content_.length());
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      starts_.clear();
      stops_.clear();
      content_.clear();
    }

    size_t
    length() const noexcept {
      return starts_.length();
    }

    bool
    is_valid(std::string& error) const noexcept {
      if (starts_.length() != stops_.length()) {
        std::stringstream out;
        out << "List node" << id_ << " has starts length " << starts_.length()
            << " but stops length " << stops_.length() << "\n";
        error.append(out.str());

        return false;
      }
      else if (stops_.length() > 0 && content_.length() != stops_.last()) {
        std::stringstream out;
        out << "List node" << id_ << " has content length " << content_.length()
            << " but last stops " << stops_.last() << "\n";
        error.append(out.str());

        return false;
      }
      else {
        return content_.is_valid(error);
      }
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      names_nbytes["node" + std::to_string(id_) + "-starts"] = starts_.nbytes();
      names_nbytes["node" + std::to_string(id_) + "-stops"] = stops_.nbytes();
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      starts_.concatenate(static_cast<PRIMITIVE*>(buffers["node" + std::to_string(id_) + "-starts"]));
      stops_.concatenate(static_cast<PRIMITIVE*>(buffers["node" + std::to_string(id_) + "-stops"]));
      content_.to_buffers(buffers);
    }

    // temporary
    void
    to_buffer(PRIMITIVE* starts, PRIMITIVE* stops) const noexcept {
      starts_.concatenate(starts);
      stops_.concatenate(stops);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"ListArray\", \"starts\": \"" + type_to_numpy_like<PRIMITIVE>()
                + "\", \"stops\": \"" + type_to_numpy_like<PRIMITIVE>() + "\", \"content\": "
                + content_.form() + params + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    GrowableBuffer<PRIMITIVE> starts_;
    GrowableBuffer<PRIMITIVE> stops_;
    BUILDER content_;
    std::string parameters_;
    size_t id_;
  };

  // EmptyLayoutBuilder
  class Empty {
  public:
    Empty() {
      size_t id = 0;
      set_id(id);
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
    }

    void
    clear() noexcept { }

    size_t
    length() const noexcept {
      return 0;
    }

    bool is_valid(std::string& /* error */) const noexcept {
      return true;
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept { }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept { }

    std::string
    form() const noexcept {
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"EmptyArray\"" + params + " }";
    }

  private:
    std::string parameters_;
    size_t id_;
  };

  // EmptyRecordLayoutBuilder
  template<bool IS_TUPLE>
  class EmptyRecord {
  public:
    EmptyRecord()
    : length_(0) {
      size_t id = 0;
      set_id(id);
    }

    void
    append() noexcept {
      length_++;
    }

    void
    extend(size_t size) noexcept {
      length_ += size;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
    }

    void
    clear() noexcept {
      length_ = 0;
    }

    size_t
    length() const noexcept {
      return length_;
    }

    bool is_valid(std::string& /* error */) const noexcept {
      return true;
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept { }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept { }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }

      if (is_tuple_) {
        return "{ \"class\": \"RecordArray\", \"contents\": []" + params
                  + ", \"form_key\": \"" + form_key.str() + "\" }";
      }
      else {
        return "{ \"class\": \"RecordArray\", \"contents\": {}" + params
                  + ", \"form_key\": \"" + form_key.str() + "\" }";
      }
    }

  private:
    std::string parameters_;
    size_t id_;
    size_t length_;
    bool is_tuple_ = IS_TUPLE;
  };

  // RecordLayoutBuilder
  template <class MAP = std::map<std::size_t, std::string>, typename... BUILDERS>
  class Record {
  public:
    using RecordContents = typename std::tuple<BUILDERS...>;
    using UserDefinedMap = MAP;

    template<std::size_t INDEX>
    using RecordFieldType = std::tuple_element_t<INDEX, RecordContents>;

    Record() {
      size_t id = 0;
      set_id(id);
      map_fields(std::index_sequence_for<BUILDERS...>());
    }

    Record(UserDefinedMap user_defined_field_id_to_name_map)
      : content_names_(user_defined_field_id_to_name_map) {
      assert(content_names_.size() == fields_count_);
      size_t id = 0;
      set_id(id);
    }

    const std::vector<std::string>
    field_names() const noexcept {
      if (content_names_.empty()) {
        return field_names_;
      } else {
        std::vector<std::string> result;
        for(auto it : content_names_) {
          result.emplace_back(it.second);
        }
        return result;
      }
    }

    void
    set_field_names(MAP user_defined_field_id_to_name_map) noexcept {
        content_names_ = user_defined_field_id_to_name_map;
    }

    template<std::size_t INDEX>
    typename RecordFieldType<INDEX>::Builder&
    field() noexcept {
      return std::get<INDEX>(contents).builder;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      for (size_t i = 0; i < fields_count_; i++) {
        visit_at(contents, i, [&id] (auto& content) { content.builder.set_id(id); });
      }
    }

    void
    clear() noexcept {
      for (size_t i = 0; i < fields_count_; i++)
        visit_at(contents, i, [](auto& content) { content.builder.clear(); });
    }

    const size_t
    length() const noexcept {
      return (std::get<0>(contents).builder.length());
    }

    bool
    is_valid(std::string& error) const noexcept {
      auto index_sequence((std::index_sequence_for<BUILDERS...>()));

      size_t length = -1;
      bool result = false;
      std::vector<size_t> lengths = field_lengths(index_sequence);
      for (size_t i = 0; i < lengths.size(); i++) {
        if (length == -1) {
          length = lengths[i];
        }
        else if (length != lengths[i]) {
          std::stringstream out;
          out << "Record node" << id_ << " has field \"" << field_names().at(i) << "\" length "
              << lengths[i] << " that differs from the first length "
              << length << "\n";
          error.append(out.str());

          return false;
        }
      }

      std::vector<bool> valid_fields = field_is_valid(index_sequence, error);
      return std::none_of(std::cbegin(valid_fields), std::cend(valid_fields), std::logical_not<bool>());
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      for (size_t i = 0; i < fields_count_; i++)
        visit_at(contents, i, [&names_nbytes](auto& content) {
          content.builder.buffer_nbytes(names_nbytes);
        });
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      for (size_t i = 0; i < fields_count_; i++)
        visit_at(contents, i, [&buffers](auto& content) {
          content.builder.to_buffers(buffers);
        });
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string("\"parameters\": { " + parameters_ + " }, ");
      }
      std::stringstream out;
      out << "{ \"class\": \"RecordArray\", \"contents\": { ";
      for (size_t i = 0;  i < fields_count_;  i++) {
        if (i != 0) {
          out << ", ";
        }
        auto contents_form = [&] (auto& content) {
          out << "\""
          << (!content_names_.empty() ? content_names_.at(content.index)
          : content.index_as_field())
          << + "\": ";
          out << content.builder.form();
        };
        visit_at(contents, i, contents_form);
      }
      out << " }, ";
      out << params << "\"form_key\": \"" << form_key.str() << "\" }";
      return out.str();
    }

    RecordContents contents;

  private:
    std::vector<std::string> field_names_;
    UserDefinedMap content_names_;
    std::string parameters_;
    size_t id_;

    static constexpr size_t fields_count_ = sizeof...(BUILDERS);

    template <std::size_t... S>
    void
    map_fields(std::index_sequence<S...>) noexcept {
      field_names_ = std::vector<std::string>({std::string(std::get<S>(contents).index_as_field())...});
    }

    template <std::size_t... S>
    std::vector<size_t>
    field_lengths(std::index_sequence<S...>) const noexcept {
      return std::vector<size_t>({std::get<S>(contents).builder.length()...});
    }

    template <std::size_t... S>
    std::vector<bool>
    field_is_valid(std::index_sequence<S...>, std::string& error) const noexcept {
      return std::vector<bool>({std::get<S>(contents).builder.is_valid(error)...});
    }

  };

  // TupleLayoutBuilder
  template <typename... BUILDERS>
  class Tuple {
    using TupleContents = typename std::tuple<BUILDERS...>;

    template<std::size_t INDEX>
    using TupleContentType = std::tuple_element_t<INDEX, TupleContents>;

  public:
    Tuple() {
      size_t id = 0;
      set_id(id);
    }

    template<std::size_t INDEX>
    TupleContentType<INDEX>&
    index() noexcept {
      return std::get<INDEX>(contents);
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      for (size_t i = 0; i < fields_count_; i++) {
        visit_at(contents, i, [&id] (auto& content) { content.set_id(id); });
      }
    }

    void
    clear() noexcept {
      for (size_t i = 0; i < fields_count_; i++)
        visit_at(contents, i, [](auto& content) { content.builder.clear(); });
    }

    const size_t
    length() const noexcept {
      return (std::get<0>(contents).builder.length());
    }

    bool
    is_valid(std::string& error) const noexcept {
      auto index_sequence((std::index_sequence_for<BUILDERS...>()));

      size_t length = -1;
      bool result = false;
      std::vector<size_t> lengths = content_lengths(index_sequence);
      for (size_t i = 0; i < lengths.size(); i++) {
        if (length == -1) {
          length = lengths[i];
        }
        else if (length != lengths[i]) {
          std::stringstream out;
          out << "Record node" << id_ << " has index \"" << i << "\" length "
              << lengths[i] << " that differs from the first length "
              << length << "\n";
          error.append(out.str());

          return false;
        }
      }

      std::vector<bool> valid_fields = content_is_valid(index_sequence, error);
      return std::none_of(std::cbegin(valid_fields), std::cend(valid_fields), std::logical_not<bool>());
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      for (size_t i = 0; i < fields_count_; i++)
        visit_at(contents, i, [&names_nbytes](auto& content) {
          content.buffer_nbytes(names_nbytes);
        });
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      for (size_t i = 0; i < fields_count_; i++)
        visit_at(contents, i, [&buffers](auto& content) {
          content.to_buffers(buffers);
        });
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string("\"parameters\": { " + parameters_ + " }, ");
      }
      std::stringstream out;
      out << "{ \"class\": \"RecordArray\", \"contents\": [";
      for (size_t i = 0;  i < fields_count_;  i++) {
        if (i != 0) {
          out << ", ";
        }
        auto contents_form = [&out] (auto& content) {
          out << content.form();
        };
        visit_at(contents, i, contents_form);
      }
      out << "], ";
      out << params << "\"form_key\": \"" << form_key.str() << "\" }";
      return out.str();
    }

    TupleContents contents;

  private:
    std::vector<int64_t> field_index_;
    std::string parameters_;
    size_t id_;

    static constexpr size_t fields_count_ = sizeof...(BUILDERS);

    template <std::size_t... S>
    std::vector<size_t>
    content_lengths(std::index_sequence<S...>) const noexcept {
      return std::vector<size_t>({std::get<S>(contents).length()...});
    }

    template <std::size_t... S>
    std::vector<bool>
    content_is_valid(std::index_sequence<S...>, std::string& error) const noexcept {
      return std::vector<bool>({std::get<S>(contents).is_valid(error)...});
    }
  };

  // RegularLayoutBuilder
  template <unsigned SIZE, typename BUILDER>
  class Regular {
  public:
    Regular()
        : length_(0) {
      size_t id = 0;
      set_id(id);
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    BUILDER&
    begin_list() noexcept {
      return content_;
    }

    void
    end_list() noexcept {
      length_++;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      content_.clear();
    }

    size_t
    length() const noexcept {
      return length_;
    }

    bool
    is_valid(std::string& error) const noexcept {
      if (content_.length() != length_ * size_) {
        std::stringstream out;
        out << "Regular node" << id_ << "has content length " << content_.length()
            << ", but length " << length_ << " and size " << size_ << "\n";
        error.append(out.str());

        return false;
      }
      else {
        return content_.is_valid(error);
      }
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      content_.to_buffers(buffers);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"RegularArray\", \"content\": " + content_.form()
                + ", \"size\": " + std::to_string(size_)  + params
                + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    BUILDER content_;
    std::string parameters_;
    size_t id_;
    size_t length_;
    size_t size_ = SIZE;
  };

  // IndexedLayoutBuilder
  template <unsigned INITIAL, typename PRIMITIVE, typename BUILDER>
  class Indexed {
  public:
    Indexed()
        : index_(awkward::GrowableBuffer<PRIMITIVE>(INITIAL))
        , last_valid_(-1) {
      size_t id = 0;
      set_id(id);
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    BUILDER&
    append_index() noexcept {
      last_valid_ = content_.length();
      index_.append(last_valid_);
      return content_;
    }

    BUILDER&
    extend_index(size_t size) noexcept {
      size_t start = content_.length();
      size_t stop = start + size;
      last_valid_ = stop - 1;
      for (size_t i = start; i < stop; i++) {
        index_.append(i);
      }
      return content_;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      last_valid_ = -1;
      index_.clear();
      content_.clear();
    }

    size_t
    length() const noexcept {
      return index_.length();
    }

    bool is_valid(std::string& error) const noexcept {
      if (content_.length() != index_.length()) {
        std::stringstream out;
        out << "Indexed node" << id_ << " has content length " << content_.length()
            << " but index length " << index_.length() << "\n";
        error.append(out.str());

        return false;
      }
      else if (content_.length() != last_valid_ + 1) {
        std::stringstream out;
        out << "Indexed node" << id_ << " has content length " << content_.length()
            << " but last valid index is " << last_valid_ << "\n";
        error.append(out.str());

        return false;
      }
      else {
        return content_.is_valid(error);
      }
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      names_nbytes["node" + std::to_string(id_) + "-index"] = index_.nbytes();
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      index_.concatenate(static_cast<PRIMITIVE*>(buffers["node" + std::to_string(id_) + "-index"]));
      content_.to_buffers(buffers);
    }

    // temporary
    void
    to_buffer(PRIMITIVE* ptr) const noexcept {
      index_.concatenate(ptr);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"IndexedArray\", \"index\": \""
                + type_to_numpy_like<PRIMITIVE>()
                + "\", \"content\": " + content_.form() + params
                + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    GrowableBuffer<PRIMITIVE> index_;
    BUILDER content_;
    std::string parameters_;
    size_t id_;
    size_t last_valid_;
  };

  // IndexedOptionLayoutBuilder
  template <unsigned INITIAL, typename PRIMITIVE, typename BUILDER>
  class IndexedOption {
  public:
    IndexedOption()
        : index_(awkward::GrowableBuffer<PRIMITIVE>(INITIAL))
        , last_valid_(-1) {
      size_t id = 0;
      set_id(id);
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    BUILDER&
    append_index() noexcept {
      last_valid_ = content_.length();
      index_.append(last_valid_);
      return content_;
    }

    BUILDER&
    extend_index(size_t size) noexcept {
      size_t start = content_.length();
      size_t stop = start + size;
      last_valid_ = stop - 1;
      for (size_t i = start; i < stop; i++) {
        index_.append(i);
      }
      return content_;
    }

    void
    append_null() noexcept {
      index_.append(-1);
    }

    void
    extend_null(size_t size) noexcept {
      for (size_t i = 0; i < size; i++) {
        index_.append(-1);
      }
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      last_valid_ = -1;
      index_.clear();
      content_.clear();
    }

    size_t
    length() const noexcept {
      return index_.length();
    }

    bool
    is_valid(std::string& error) const noexcept {
      if (content_.length() != last_valid_ + 1) {
        std::stringstream out;
        out << "IndexedOption node" << id_ << " has content length "<< content_.length()
            << " but last valid index is " << last_valid_ << "\n";
        error.append(out.str());

        return false;
      }
      else {
        return content_.is_valid(error);
      }
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      names_nbytes["node" + std::to_string(id_) + "-index"] = index_.nbytes();
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      index_.concatenate(static_cast<PRIMITIVE*>(buffers["node" + std::to_string(id_) + "-index"]));
      content_.to_buffers(buffers);
    }

    // temporary
    void
    to_buffer(PRIMITIVE* ptr) const noexcept {
      index_.concatenate(ptr);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"IndexedOptionArray\", \"index\": \""
                + type_to_numpy_like<PRIMITIVE>()
                + "\", \"content\": " + content_.form() + params
                + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    GrowableBuffer<PRIMITIVE> index_;
    BUILDER content_;
    std::string parameters_;
    size_t id_;
    size_t last_valid_;
  };

  // UnmaskedLayoutBuilder
  template <typename BUILDER>
  class Unmasked {
  public:
    Unmasked() {
      size_t id = 0;
      set_id(id);
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    BUILDER&
    append_valid() noexcept {
      return content_;
    }

    BUILDER&
    extend_valid(size_t size) noexcept {
      return content_;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      content_.clear();
    }

    size_t
    length() const noexcept {
      return content_.length();
    }

    bool is_valid(std::string& error) const noexcept {
      return content_.is_valid(error);
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      content_.to_buffers(buffers);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"UnmaskedArray\", \"content\": " + content_.form()
                + params + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    BUILDER content_;
    std::string parameters_;
    size_t id_;
  };

  // ByteMaskedLayoutBuilder
  template <unsigned INITIAL, bool VALID_WHEN, typename BUILDER>
  class ByteMasked {
  public:
    ByteMasked()
        : mask_(awkward::GrowableBuffer<int8_t>(INITIAL)) {
      size_t id = 0;
      set_id(id);
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    bool
    valid_when() const noexcept {
      return valid_when_;
    }

    BUILDER&
    append_valid() noexcept {
      mask_.append(valid_when_);
      return content_;
    }

    BUILDER&
    extend_valid(size_t size) noexcept {
      for (size_t i = 0; i < size; i++) {
        mask_.append(valid_when_);
      }
      return content_;
    }

    BUILDER&
    append_null() noexcept {
      mask_.append(!valid_when_);
      return content_;
    }

    BUILDER&
    extend_null(size_t size) noexcept {
      for (size_t i = 0; i < size; i++) {
        mask_.append(!valid_when_);
      }
      return content_;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      mask_.clear();
      content_.clear();
    }

    size_t
    length() const noexcept {
      return mask_.length();
    }

    bool
    is_valid(std::string& error) const noexcept {
      if (content_.length() != mask_.length()) {
        std::stringstream out;
        out << "ByteMasked node" << id_ << "has content length " << content_.length()
            << "but mask length " << mask_.length() << "\n";
        error.append(out.str());

        return false;
      }
      else {
        return content_.is_valid(error);
      }
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      names_nbytes["node" + std::to_string(id_) + "-mask"] = mask_.nbytes();
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      mask_.concatenate(static_cast<int8_t*>(buffers["node" + std::to_string(id_) + "-mask"]));
      content_.to_buffers(buffers);
    }

    // temporary
    void
    to_buffer(int8_t* ptr) const noexcept {
      mask_.concatenate(ptr);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key, form_valid_when;
      form_key << "node" << id_;
      form_valid_when << std::boolalpha << valid_when_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"ByteMaskedArray\", \"mask\": \"i8\", \"content\": " + content_.form()
                + ", \"valid_when\": " + form_valid_when.str()
                + params + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    GrowableBuffer<int8_t> mask_;
    BUILDER content_;
    std::string parameters_;
    size_t id_;
    bool valid_when_ = VALID_WHEN;
  };

  // FIXME: mask value incorrect
  // BitMaskedLayoutBuilder
  template <unsigned INITIAL, bool VALID_WHEN, bool LSB_ORDER, typename BUILDER>
  class BitMasked {
  public:
    BitMasked()
        : mask_(awkward::GrowableBuffer<uint8_t>(INITIAL))
        , current_byte_(uint8_t(0))
        , current_byte_ref_(mask_.append_and_get_ref(current_byte_))
        , current_index_(0)
      {
      size_t id = 0;
      set_id(id);
      if (lsb_order_) {
        for(size_t i = 0; i < 8; i++) {
          cast_[i] = 1 << i;
        }
      }
      else {
        for(size_t i = 0; i < 8; i++) {
          cast_[i] = 128 >> i;
        }
      }
    }

    BUILDER&
    content() noexcept {
      return content_;
    }

    bool
    valid_when() const noexcept {
      return valid_when_;
    }

    bool
    lsb_order() const noexcept {
      return lsb_order_;
    }

    BUILDER&
    append_valid() noexcept {
      append_begin();
      current_byte_ |= cast_[current_index_];
      append_end();
      return content_;
    }

    BUILDER&
    extend_valid(size_t size) noexcept {
      for (size_t i = 0; i < size; i++) {
        append_valid();
      }
      return content_;
    }

    BUILDER&
    append_null() noexcept {
      append_begin();
      append_end();
      return content_;
    }

    BUILDER&
    extend_null(size_t size) noexcept {
      for (size_t i = 0; i < size; i++) {
      append_null();
      }
      return content_;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      content_.set_id(id);
    }

    void
    clear() noexcept {
      mask_.clear();
      content_.clear();
    }

    size_t
    length() const noexcept {
      return (mask_.length() - 1) * 8 + current_index_;
    }

    bool
    is_valid(std::string& error) const noexcept {
      if (content_.length() != length()) {
        std::stringstream out;
        out << "BitMasked node" << id_ << "has content length " << content_.length()
            << "but bit mask length " << mask_.length() << "\n";
        error.append(out.str());

        return false;
      }
      else {
        return content_.is_valid(error);
      }
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      names_nbytes["node" + std::to_string(id_) + "-mask"] = mask_.nbytes();
      content_.buffer_nbytes(names_nbytes);
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      mask_.concatenate(static_cast<uint8_t*>(buffers["node" + std::to_string(id_) + "-mask"]));
      content_.to_buffers(buffers);
    }

    // temporary
    void
    to_buffer(uint8_t* ptr) const noexcept {
      mask_.concatenate(ptr);
    }

    std::string
    form() const noexcept {
      std::stringstream form_key, form_valid_when, form_lsb_order;
      form_key << "node" << id_;
      form_valid_when << std::boolalpha << valid_when_;
      form_lsb_order << std::boolalpha << lsb_order_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      return "{ \"class\": \"BitMaskedArray\", \"mask\": \"u8\", \"content\": " + content_.form()
                + ", \"valid_when\": " + form_valid_when.str() + ", \"lsb_order\": "
                + form_lsb_order.str() + params + ", \"form_key\": \"" + form_key.str() + "\" }";
    }

  private:
    void
    append_begin() {
      if (current_index_ == 8) {
        current_byte_ref_ = mask_.append_and_get_ref(current_byte_);
        current_byte_ = uint8_t(0);
        current_index_ = 0;
      }
    }

    void
    append_end() {
      current_index_ += 1;
      if (valid_when_) {
        current_byte_ref_ = current_byte_;
      }
      else {
        current_byte_ref_ = ~current_byte_;
      }
    }

    GrowableBuffer<uint8_t> mask_;
    BUILDER content_;
    std::string parameters_;
    size_t id_;
    uint8_t current_byte_;
    uint8_t& current_byte_ref_;
    size_t current_index_;
    uint8_t cast_[8];
    bool valid_when_ = VALID_WHEN;
    bool lsb_order_ = LSB_ORDER;
  };

  // UnionLayoutBuilder
  template <unsigned INITIAL, typename TAGS, typename INDEX,  typename... BUILDERS>
  class Union {
  public:
    using Contents = typename std::tuple<BUILDERS...>;

    template<std::size_t I>
    using ContentType = std::tuple_element_t<I, Contents>;

    Union()
        : tags_(awkward::GrowableBuffer<TAGS>(INITIAL))
        , index_(awkward::GrowableBuffer<INDEX>(INITIAL)) {
      size_t id = 0;
      set_id(id);
      for (size_t i = 0; i < contents_count_; i++)
        last_valid_index_[i] = -1;
    }

    template<std::size_t I>
    ContentType<I>&
    content() noexcept {
      return std::get<I>(contents_);
    }

    template<std::size_t TAG>
    ContentType<TAG>&
    append_index() noexcept {
      auto& which_content = std::get<TAG>(contents_);
      INDEX next_index = which_content.length();

      TAGS tag = (TAGS)TAG;
      last_valid_index_[tag] = next_index;
      tags_.append(tag);
      index_.append(next_index);

      return which_content;
    }

    const std::string&
    parameters() const noexcept {
      return parameters_;
    }

    void
    set_parameters(std::string parameter) noexcept {
      parameters_ = parameter;
    }

    void
    set_id(size_t &id) noexcept {
      id_ = id;
      id++;
      auto contents_id = [&id](auto& content) { content.set_id(id); };
      for (size_t i = 0; i < contents_count_; i++)
        visit_at(contents_, i, contents_id);
    }

    void
    clear() noexcept {
      for (size_t i = 0; i < contents_count_; i++)
        last_valid_index_[i] = -1;
      tags_.clear();
      index_.clear();
      auto clear_contents = [](auto& content) { content.builder.clear(); };
      for (size_t i = 0; i < contents_count_; i++)
        visit_at(contents_, i, clear_contents);
    }

    size_t
    length() const noexcept {
      return tags_.length();
    }

    bool
    is_valid(std::string& error) const noexcept {
      auto index_sequence((std::index_sequence_for<BUILDERS...>()));

      std::vector<size_t> lengths = content_lengths(index_sequence);
      for (size_t tag = 0; tag < contents_count_; tag++) {
        if (lengths[tag] != last_valid_index_[tag] + 1) {
          std::stringstream out;
          out << "Union node" << id_ << " has content length " << lengths[tag]
              << " but index length " << last_valid_index_[tag] << "\n";
          error.append(out.str());

          return false;
        }
      }

      std::vector<bool> valid_contents = content_is_valid(index_sequence, error);
      return std::none_of(std::cbegin(valid_contents), std::cend(valid_contents), std::logical_not<bool>());
    }

    void
    buffer_nbytes(std::map<std::string, size_t> &names_nbytes) const noexcept {
      auto index_sequence((std::index_sequence_for<BUILDERS...>()));

      names_nbytes["node" + std::to_string(id_) + "-tags"] = tags_.nbytes();
      names_nbytes["node" + std::to_string(id_) + "-index"] = index_.nbytes();

      for (size_t i = 0; i < contents_count_; i++)
        visit_at(contents_, i, [&names_nbytes](auto& content) {
          content.buffer_nbytes(names_nbytes);
        });
    }

    void
    to_buffers(std::map<std::string, void*> &buffers) const noexcept {
      auto index_sequence((std::index_sequence_for<BUILDERS...>()));

      tags_.concatenate(static_cast<TAGS*>(buffers["node" + std::to_string(id_) + "-tags"]));
      index_.concatenate(static_cast<INDEX*>(buffers["node" + std::to_string(id_) + "-index"]));

      for (size_t i = 0; i < contents_count_; i++)
        visit_at(contents_, i, [&buffers](auto& content) {
          content.to_buffers(buffers);
        });
    }

    std::string
    form() const noexcept {
      std::stringstream form_key;
      form_key << "node" << id_;
      std::string params("");
      if (parameters_ == "") { }
      else {
        params = std::string(", \"parameters\": { " + parameters_ + " }");
      }
      std::stringstream out;
      out << "{ \"class\": \"UnionArray\", \"tags\": \""
                + type_to_numpy_like<TAGS>() + "\", \"index\": \""
                + type_to_numpy_like<INDEX>() + "\", \"contents\": [";
      for (size_t i = 0;  i < contents_count_;  i++) {
        if (i != 0) {
          out << ", ";
        }
        auto contents_form = [&] (auto& content) {
          out << content.form();
        };
        visit_at(contents_, i, contents_form);
      }
      out << "], ";
      out << params << "\"form_key\": \"" << form_key.str() << "\" }";
      return out.str();
    }

  private:
    static constexpr size_t contents_count_ = sizeof...(BUILDERS);

    GrowableBuffer<TAGS> tags_;
    GrowableBuffer<INDEX> index_;
    Contents contents_;
    size_t id_;
    std::string parameters_;
    size_t last_valid_index_[sizeof...(BUILDERS)];

    template <std::size_t... S>
    std::vector<size_t>
    content_lengths(std::index_sequence<S...>) const {
      return std::vector<size_t>({std::get<S>(contents_).length()...});
    }

    template <std::size_t... S>
    std::vector<bool>
    content_is_valid(std::index_sequence<S...>, std::string& error) const {
      return std::vector<bool>({std::get<S>(contents_).is_valid(error)...});
    }

  };

  }  // namespace LayoutBuilder
}  // namespace awkward

#endif  // AWKWARD_LAYOUTBUILDER_H_