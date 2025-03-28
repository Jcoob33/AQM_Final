/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Moatamri Faker <faker.moatamri@sophia.inria.fr>
 */

#include "attribute-default-iterator.h"

#include "ns3/type-id.h"

#include <gtk/gtk.h>
#include <vector>

namespace ns3
{

enum
{
    COL_TYPEID = 0,
    COL_LASTID
};

/**
 * @ingroup configstore
 * @brief A class used in the implementation of the GtkConfigStore
 */
struct ModelTypeid
{
    /**
     * @brief Whether the node represents an attribute or TypeId
     */
    enum
    {
        // store TypeId + attribute name +defaultValue and index
        NODE_ATTRIBUTE,
        // store TypeId
        NODE_TYPEID
    } type; ///< node type

    /// TypeId name
    std::string name;
    /// TypeId default value
    std::string defaultValue;
    /// The TypeId object and if it is an attribute, it's the TypeId object of the attribute
    TypeId tid;
    /// stores the index of the attribute in list of attributes for a given TypeId
    uint32_t index;
};

/**
 * @ingroup configstore
 * @brief ModelTypeIdCreator class
 */
class ModelTypeidCreator : public AttributeDefaultIterator
{
  public:
    ModelTypeidCreator();
    /**
     * @brief This method will iterate on typeIds having default attributes and create a model
     * for them, this model will be used by the view.
     *
     * @param treestore the GtkTreeStore.
     */
    void Build(GtkTreeStore* treestore);

  private:
    /**
     * @brief This method will add a ModelTypeid to the GtkTreeIterator
     * @param tid TypeId
     * @param name attribute name
     * @param defaultValue default value
     * @param index index of the attribute in the specified Typeid
     */
    void VisitAttribute(TypeId tid,
                        std::string name,
                        std::string defaultValue,
                        uint32_t index) override;
    /**
     * @brief Add a node for the new TypeId object
     * @param name TypeId name
     */
    void StartVisitTypeId(std::string name) override;
    /**
     * @brief Remove the last gtk tree iterator
     */
    void EndVisitTypeId() override;
    /**
     * @brief Adds a treestore iterator to m_treestore model
     * @param node the node to be added
     */
    void Add(ModelTypeid* node);
    /**
     * Removes the last GtkTreeIterator from m_iters
     */
    void Remove();
    /// this is the TreeStore model corresponding to the view
    GtkTreeStore* m_treestore;
    /// This contains a vector of iterators used to build the TreeStore
    std::vector<GtkTreeIter*> m_iters;
};
} // namespace ns3
