#ifndef GRAPHLEARN_CORE_GRAPH_STORAGE_VINEYARD_NODE_STORAGE_H_
#define GRAPHLEARN_CORE_GRAPH_STORAGE_VINEYARD_NODE_STORAGE_H_

#if defined(WITH_VINEYARD)
#include "vineyard/graph/fragment/arrow_fragment.h"
#endif

#if defined(WITH_VINEYARD)
#include "graphlearn/include/config.h"
#include "graphlearn/core/graph/storage/node_storage.h"
#include "graphlearn/core/graph/storage/vineyard_storage_utils.h"

namespace graphlearn {
namespace io {

class VineyardNodeStorage : public graphlearn::io::NodeStorage {
public:
  explicit VineyardNodeStorage(std::string const &node_label="0")
      : VineyardNodeStorage(std::stoi(node_label)) {
  }

  explicit VineyardNodeStorage(label_id_t const node_label=0)
      : node_label_(node_label) {
    std::cerr << "node_label = " << node_label << ", from " << GLOBAL_FLAG(VineyardGraphID) << std::endl;
    VINEYARD_CHECK_OK(client_.Connect(GLOBAL_FLAG(VineyardIPCSocket)));
    frag_ = client_.GetObject<gl_frag_t>(GLOBAL_FLAG(VineyardGraphID));
  }

  virtual ~VineyardNodeStorage() = default;

  virtual void Lock() override {}
  virtual void Unlock() override {}

  virtual void SetSideInfo(const SideInfo *info) override {}
  virtual const SideInfo *GetSideInfo() const override {
    std::cerr << "node: get sideinfo" << std::endl;
    return frag_node_side_info(frag_, node_label_);
  }

  /// Do some re-organization after data fixed.
  virtual void Build() override {}

  /// Get the total node count after data fixed.
  virtual IdType Size() const override {
    std::cerr << "node: get size = " << frag_->vertex_data_table(node_label_)->num_rows() << std::endl;
    return frag_->vertex_data_table(node_label_)->num_rows();
  }

  /// A NODE is made up of [ id, attributes, weight, label ].
  /// Insert a node. If a node with the same id existed, just ignore.
  virtual void Add(NodeValue *value) override {}

  /// Lookup node infos by node_id, including
  ///    node weight,
  ///    node label,
  ///    node attributes
  virtual float GetWeight(IdType node_id) const override {
    auto v = vertex_t{node_id};
    auto table = frag_->vertex_data_table(frag_->vertex_label(v));
    int index = find_index_of_name(table->schema(), "weight");
    if (index == -1) {
      std::cerr << "weight not available for node " << node_id << std::endl;
      return 0.0;
    }
    return static_cast<float>(frag_->GetData<double>(vertex_t{node_id}, index));
  }

  virtual int32_t GetLabel(IdType node_id) const override {
    auto v = vertex_t{node_id};
    auto table = frag_->vertex_data_table(frag_->vertex_label(v));
    int index = find_index_of_name(table->schema(), "label");
    if (index == -1) {
      std::cerr << "label not available for node " << node_id << std::endl;
      return 0;
    }
    return static_cast<float>(frag_->GetData<int64_t>(vertex_t{node_id}, index));
  }

  virtual Attribute GetAttribute(IdType node_id) const override {
    auto v = vertex_t{node_id};
    auto label = frag_->vertex_label(v);
    auto offset = frag_->vertex_offset(v);
    auto table = frag_->vertex_data_table(label);
    return Attribute(arrow_line_to_attribute_value(table, offset, 0), true);
  }

  /// For the needs of traversal and sampling, the data distribution is
  /// helpful. The interface should make it convenient to get the global data.
  ///
  /// Get all the node ids, the count of which is the same with Size().
  /// These ids are distinct.
  virtual const IdList *GetIds() const override {
    std::cerr << "node: get ids: " << node_label_ << std::endl;
    size_t count = frag_->GetInnerVerticesNum(node_label_);
    auto id_list = new IdList();
    id_list->reserve(count);
    std::cerr << "node: get ids: count = " << count << std::endl;

    auto id_range = frag_->InnerVertices(node_label_);
    for (auto id = id_range.begin(); id < id_range.end(); ++id) {
      id_list->emplace_back(id.GetValue());
    }
    return id_list;
  }

  /// Get all weights if existed, the count of which is the same with Size().
  virtual const std::vector<float> *GetWeights() const override {
    return GetAttribute<double, float>("weight");
  }

  /// Get all labels if existed, the count of which is the same with Size().
  virtual const std::vector<int32_t> *GetLabels() const override {
    return GetAttribute<int64_t, int32_t>("label");
  }

  /// Get all attributes if existed, the count of which is the same with Size().
  virtual const std::vector<Attribute> *GetAttributes() const override {
    std::cerr << "node: get attributes: node_label = " << node_label_ << std::endl;
    size_t count = frag_->GetInnerVerticesNum(node_label_);
    std::cerr << "node: get attributes: count = " << count << std::endl;

    auto value_list = new std::vector<Attribute>();
    value_list->reserve(count);

    auto id_range = frag_->InnerVertices(node_label_);
    auto vtable = frag_->vertex_data_table(node_label_);
    for (auto id = id_range.begin(); id < id_range.end(); ++id) {
      auto offset = frag_->vertex_offset(id);
      value_list->emplace_back(
          arrow_line_to_attribute_value(vtable, offset, 0), true);
    }
    return value_list;
  }

private:
  template <typename T, typename RT=T>
  const std::vector<RT> *GetAttribute(std::string const &name) const {
    int attr_index = find_index_of_name(
        frag_->vertex_data_table(node_label_)->schema(), name);
    if (attr_index == -1) {
      return nullptr;
    }
    size_t count = frag_->GetInnerVerticesNum(node_label_);
    auto value_list = new std::vector<RT>();
    value_list->reserve(count);

    auto id_range = frag_->InnerVertices(node_label_);
    for (auto id = id_range.begin(); id < id_range.end(); ++id) {
      value_list->emplace_back(frag_->GetData<T>(id, attr_index));
    }
    return value_list;
  }

  vineyard::Client client_;
  std::shared_ptr<gl_frag_t> frag_;
  label_id_t node_label_;
};

} // namespace io
} // namespace graphlearn

#endif

#endif // GRAPHLEARN_CORE_GRAPH_STORAGE_VINEYARD_NODE_STORAGE_H_
