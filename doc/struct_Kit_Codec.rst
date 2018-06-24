.. index:: pair: struct; Kit_Codec
.. _doxid-struct_kit___codec:
.. _cid-kit_codec:

struct Kit_Codec
================

.. toctree::
	:hidden:

.. code-block:: cpp
	:class: overview-code-block

	#include <kitcodec.h>


Overview
~~~~~~~~



.. ref-code-block:: cpp
	:class: overview-code-block

	// fields

	unsigned int :ref:`threads<doxid-struct_kit___codec_1a936dd913b2909941e76f5557b1d58513>`
	char :ref:`name<doxid-struct_kit___codec_1ad4f851cd09bb98a6f627ce89012535df>`[KIT_CODEC_NAME_MAX]
	char :ref:`description<doxid-struct_kit___codec_1a541467f595a7146e131297fb7a34a177>`[KIT_CODEC_DESC_MAX]

.. _details-doxid-struct_kit___codec:

Detailed Documentation
~~~~~~~~~~~~~~~~~~~~~~



Fields
------

.. _doxid-struct_kit___codec_1a936dd913b2909941e76f5557b1d58513:
.. _cid-kit_codec::threads:
.. ref-code-block:: cpp
	:class: title-code-block

	unsigned int threads

Currently enabled threads.

.. _doxid-struct_kit___codec_1ad4f851cd09bb98a6f627ce89012535df:
.. _cid-kit_codec::name:
.. ref-code-block:: cpp
	:class: title-code-block

	char name [KIT_CODEC_NAME_MAX]

Codec short name, eg. "ogg" or "webm".

.. _doxid-struct_kit___codec_1a541467f595a7146e131297fb7a34a177:
.. _cid-kit_codec::description:
.. ref-code-block:: cpp
	:class: title-code-block

	char description [KIT_CODEC_DESC_MAX]

Codec longer, more descriptive name.

