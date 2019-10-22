/****************************************************************************
**
** Copyright (C) 2015 Paul Lemire
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

#include "loadbufferjob_p.h"
#include <Qt3DRender/private/buffer_p.h>
#include <Qt3DRender/private/qbuffer_p.h>
#include <Qt3DRender/private/buffermanager_p.h>
#include <Qt3DRender/private/renderer_p.h>
#include <Qt3DRender/private/job_common_p.h>
#include <Qt3DCore/private/qaspectmanager_p.h>

QT_BEGIN_NAMESPACE

namespace Qt3DRender {

namespace Render {

class LoadBufferJobPrivate : public Qt3DCore::QAspectJobPrivate
{
public:
    LoadBufferJobPrivate() {}
    ~LoadBufferJobPrivate() {}

    void postFrame(Qt3DCore::QAspectManager *aspectManager) override;

    Buffer *m_bufferToUpdate = nullptr;
};


LoadBufferJob::LoadBufferJob(const HBuffer &handle)
    : QAspectJob(*new LoadBufferJobPrivate)
    , m_handle(handle)
    , m_nodeManagers(nullptr)
{
    SET_JOB_RUN_STAT_TYPE(this, JobTypes::LoadBuffer, 0)
}

LoadBufferJob::~LoadBufferJob()
{
}

void LoadBufferJob::run()
{
    Q_DJOB(LoadBufferJob);
    // Let's leave it for the moment until this has been properly tested
    qCDebug(Jobs) << Q_FUNC_INFO;
    Buffer *buffer = m_nodeManagers->data<Buffer, BufferManager>(m_handle);
    buffer->executeFunctor();
    if (buffer->isSyncData())
        d->m_bufferToUpdate = buffer;
}

void LoadBufferJobPrivate::postFrame(Qt3DCore::QAspectManager *aspectManager)
{
    if (m_bufferToUpdate == nullptr)
        return;
    QBuffer *frontendBuffer = static_cast<decltype(frontendBuffer)>(aspectManager->lookupNode(m_bufferToUpdate->peerId()));
    QBufferPrivate *dFrontend = static_cast<decltype(dFrontend)>(Qt3DCore::QNodePrivate::get(frontendBuffer));
    // Calling frontendBuffer->setData would result in forcing a sync against the backend
    // which isn't necessary
    dFrontend->setData(m_bufferToUpdate->data());
    m_bufferToUpdate = nullptr;
}

} // namespace Render

} // namespace Qt3DRender

QT_END_NAMESPACE
