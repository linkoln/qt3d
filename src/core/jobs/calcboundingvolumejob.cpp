/****************************************************************************
**
** Copyright (C) 2020 Klaralvdalens Datakonsult AB (KDAB).
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "calcboundingvolumejob_p.h"

#include <Qt3DCore/qattribute.h>
#include <Qt3DCore/qboundingvolume.h>
#include <Qt3DCore/qbuffer.h>
#include <Qt3DCore/qgeometryview.h>
#include <Qt3DCore/private/job_common_p.h>
#include <Qt3DCore/private/qaspectjob_p.h>
#include <Qt3DCore/private/qaspectmanager_p.h>
#include <Qt3DCore/private/qattribute_p.h>
#include <Qt3DCore/private/qboundingvolume_p.h>
#include <Qt3DCore/private/qbuffer_p.h>
#include <Qt3DCore/private/qentity_p.h>
#include <Qt3DCore/private/qgeometry_p.h>
#include <Qt3DCore/private/qgeometryview_p.h>
#include <Qt3DCore/private/qnodevisitor_p.h>
#include <Qt3DCore/private/qthreadpooler_p.h>

#include <QtCore/qmath.h>
#if QT_CONFIG(concurrent)
#include <QtConcurrent/QtConcurrent>
#endif

QT_BEGIN_NAMESPACE

namespace Qt3DCore {

namespace {

BoundingVolumeComputeData findBoundingVolumeComputeData(QGeometryView *node)
{
    if (!node->isEnabled())
        return {};

    if (node->primitiveType() == QGeometryView::Patches)
        return {};

    QGeometry *geom = node->geometry();
    QGeometryPrivate *dgeom = QGeometryPrivate::get(geom);
    if (!geom)
        return {};

    int drawVertexCount = node->vertexCount(); // may be 0, gets changed below if so

    QAttribute *positionAttribute = dgeom->m_boundingVolumePositionAttribute;
    const QList<Qt3DCore::QAttribute *> attributes = geom->attributes();

    // Use the default position attribute if attribute is null
    if (!positionAttribute) {
        for (QAttribute *attr : attributes) {
            if (attr->name() == QAttribute::defaultPositionAttributeName()) {
                positionAttribute = attr;
                break;
            }
        }
    }

    if (!positionAttribute
        || positionAttribute->attributeType() != QAttribute::VertexAttribute
        || positionAttribute->vertexBaseType() != QAttribute::Float
        || positionAttribute->vertexSize() < 3) {
        qWarning("findBoundingVolumeComputeData: Position attribute not suited for bounding volume computation");
        return {};
    }

    Qt3DCore::QBuffer *positionBuffer = positionAttribute->buffer();
    // No point in continuing if the positionAttribute doesn't have a suitable buffer
    if (!positionBuffer) {
        qWarning("findBoundingVolumeComputeData: Position attribute not referencing a valid buffer");
        return {};
    }

    // Check if there is an index attribute.
    QAttribute *indexAttribute = nullptr;
    Qt3DCore::QBuffer *indexBuffer = nullptr;

    for (const auto attr : attributes) {
        if (attr->attributeType() == QAttribute::IndexAttribute) {
            indexBuffer = attr->buffer();
            if (indexBuffer) {
                indexAttribute = attr;

                if (!drawVertexCount)
                    drawVertexCount = static_cast<int>(indexAttribute->count());

                static const QAttribute::VertexBaseType validIndexTypes[] = {
                    QAttribute::UnsignedShort,
                    QAttribute::UnsignedInt,
                    QAttribute::UnsignedByte
                };

                if (std::find(std::begin(validIndexTypes),
                              std::end(validIndexTypes),
                              indexAttribute->vertexBaseType()) == std::end(validIndexTypes)) {
                    qWarning() << "findBoundingVolumeComputeData: Unsupported index attribute type" << indexAttribute->name() << indexAttribute->vertexBaseType();
                    return {};
                }

                break;
            }
        }
    }

    if (!indexAttribute && !drawVertexCount)
        drawVertexCount = static_cast<int>(positionAttribute->count());

    return { nullptr, nullptr, positionAttribute, indexAttribute, drawVertexCount };
}

bool isTreeEnabled(QEntity *entity) {
    if (!entity->isEnabled())
        return false;

    QEntity *parent = entity->parentEntity();
    while (parent) {
        if (!parent->isEnabled())
            return false;
        parent = parent->parentEntity();
    }

    return true;
}

struct UpdateBoundFunctor
{
    // This define is required to work with QtConcurrent
    typedef std::vector<BoundingVolumeComputeResult> result_type;
    result_type operator ()(const BoundingVolumeComputeData &data)
    {
        return { data.compute() };
    }
};

struct ReduceUpdateBoundFunctor
{
    void operator ()(std::vector<BoundingVolumeComputeResult> &result, const std::vector<BoundingVolumeComputeResult> &values)
    {
        result.insert(result.end(),
                      std::make_move_iterator(values.begin()),
                      std::make_move_iterator(values.end()));
    }
};

} // anonymous


BoundingVolumeComputeData BoundingVolumeComputeData::fromView(QGeometryView *view)
{
    return findBoundingVolumeComputeData(view);
}

BoundingVolumeComputeResult BoundingVolumeComputeData::compute() const
{
    BoundingVolumeCalculator calculator;
    if (calculator.apply(positionAttribute, indexAttribute, vertexCount,
                         provider->view()->primitiveRestartEnabled(),
                         provider->view()->restartIndexValue()))
        return {
            entity, provider, positionAttribute, indexAttribute,
            calculator.min(), calculator.max(),
            calculator.center(), calculator.radius()
        };
    return {};
}


CalculateBoundingVolumeJob::CalculateBoundingVolumeJob()
    : Qt3DCore::QAspectJob()
    , m_root(nullptr)
{
    SET_JOB_RUN_STAT_TYPE(this, JobTypes::CalcBoundingVolume, 0)
}

void CalculateBoundingVolumeJob::run()
{
    m_results.clear();

    QHash<QEntity *, BoundingVolumeComputeData> dirtyEntities;
    QNodeVisitor visitor;
    visitor.traverse(m_root, [](QNode *) {}, [&dirtyEntities](QEntity *entity) {
        if (!isTreeEnabled(entity))
            return;

        const auto bvProviders = entity->componentsOfType<QBoundingVolume>();
        if (bvProviders.isEmpty())
            return;

        // we go through the list until be find a dirty provider,
        // or THE primary provider
        bool foundBV = false;
        for (auto bv: bvProviders) {
            auto dbv = QBoundingVolumePrivate::get(bv);
            if (foundBV && !dbv->m_primaryProvider)
                continue;

            BoundingVolumeComputeData bvdata;
            if (!dbv->m_explicitPointsValid && bv->view()) {
                bvdata = findBoundingVolumeComputeData(bv->view());
                if (!bvdata.valid())
                    continue;
                bvdata.entity = entity;
                bvdata.provider = bv;
            } else {
                // bounds are explicitly set, don't bother computing
                // or no view, can't compute
                continue;
            }

            bool dirty = QEntityPrivate::get(entity)->m_dirty;
            dirty |= QGeometryViewPrivate::get(bv->view())->m_dirty;
            dirty |= QGeometryPrivate::get(bv->view()->geometry())->m_dirty;
            dirty |= QAttributePrivate::get(bvdata.positionAttribute)->m_dirty;
            dirty |= QBufferPrivate::get(bvdata.positionAttribute->buffer())->m_dirty;
            if (bvdata.indexAttribute) {
                dirty |= QAttributePrivate::get(bvdata.indexAttribute)->m_dirty;
                dirty |= QBufferPrivate::get(bvdata.indexAttribute->buffer())->m_dirty;
            }

            if (dbv->m_primaryProvider) {
                if (dirty)
                    dirtyEntities[entity] = bvdata;
                break;
            } else if (dirty) {
                dirtyEntities[entity] = bvdata;
                foundBV = true;
            }
        }
    });

#if QT_CONFIG(concurrent)
    if (dirtyEntities.size() > 1 && QThreadPooler::maxThreadCount() > 1) {
        UpdateBoundFunctor functor;
        ReduceUpdateBoundFunctor reduceFunctor;
        m_results = QtConcurrent::blockingMappedReduced<decltype(m_results)>(dirtyEntities, functor, reduceFunctor);
    } else
#endif
    {
        for (auto it = dirtyEntities.begin(); it != dirtyEntities.end(); ++it) {
            auto res = it.value().compute();
            if (res.valid())
                m_results.push_back(res); // How do we push it to the backends????
        }
    }
}

void CalculateBoundingVolumeJob::postFrame(QAspectEngine *aspectEngine)
{
    Q_UNUSED(aspectEngine);

    for (auto result: m_results) {
        // set the results
        QBoundingVolumePrivate::get(result.provider)->setImplicitBounds(result.m_min, result.m_max, result.m_center, result.m_radius);

        // reset dirty flags
        QEntityPrivate::get(result.entity)->m_dirty = false;
        QGeometryViewPrivate::get(result.provider->view())->m_dirty = false;
        QGeometryPrivate::get(result.provider->view()->geometry())->m_dirty = false;
        QAttributePrivate::get(result.positionAttribute)->m_dirty = false;
        QBufferPrivate::get(result.positionAttribute->buffer())->m_dirty = false;
        if (result.indexAttribute) {
            QAttributePrivate::get(result.indexAttribute)->m_dirty = false;
            QBufferPrivate::get(result.indexAttribute->buffer())->m_dirty = false;
        }
    }

    m_results.clear();
}

} // namespace Qt3DCore

QT_END_NAMESPACE

